#include <string.h>

#include "ch.h"
#include "hal.h"

#include "nand_ring.h"
#include "timeboot_u64.h"

/*
 * После запуска считаем, что предыдущий раз был неудачен по причине
 * отключения электричества, поэтому конец лога записываем заново:
 * 1) ощищаем следующий блок
 * 2) копируем заведомо целые страницы в него
 * 3) ощищаем предыдущий
 * 4) копируем страницы обратно
 *
 * На старте сканируем первые страницы блоков с нулевого по последний
 * 1) Последним считаем блок с максимальным номером
 * 2) Если номера совпадают - значит был прерван процесс восстановления: повторяем его.
 *
 * Форматирование - это простая очистка всех блоков
 *
 * Если при записи память обнаружила ошибку - перемещаем записанные данные
 * в новый блок, текущий помечаем, как сбойный
 *
 * Если ошибка обнаружилась при чтении - пытаемся скорректировать данные. Блок
 * НЕ бракуем.
 */

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */
#define PAGE_ID_ERASED            (uint64_t)0xFFFFFFFFFFFFFFFF
#define COMPILE_TIME_UTC          1400000000
#define MIN_RING_BLOCKS           16 // minimal meaningful storage size
#define MIN_GOOD_BLOCKS           4 // minimal allowable good blocks count

#define SCRATCHPAD_SIZE           (2048 + 64)

/*
 ******************************************************************************
 * EXTERNS
 ******************************************************************************
 */

/*
 ******************************************************************************
 * PROTOTYPES
 ******************************************************************************
 */

/*
 ******************************************************************************
 * GLOBAL VARIABLES
 ******************************************************************************
 */

static uint8_t scratchpad[SCRATCHPAD_SIZE];

/*
 ******************************************************************************
 ******************************************************************************
 * LOCAL FUNCTIONS
 ******************************************************************************
 ******************************************************************************
 */

/**
 *
 */
static uint32_t next_good(NandRing *ring, uint32_t current) {

  const uint32_t end_blk = ring->config->end_blk;
  const uint32_t start_blk = ring->config->start_blk;
  NANDDriver *nandp = ring->config->nandp;

  while (true) {
    current++;
    if (current > end_blk) {
      current = start_blk;
    }
    if (! nandIsBad(nandp, current)) {
      return current;
    }
  }
}

/**
 * @brief NandRing::get_session_num
 * @param blk
 * @param page
 * @return
 */
static uint8_t read_session_num(NandRing *ring, uint32_t blk, uint32_t page) {
  NandPageHeader tmp;
  nandReadPageSpare(ring->config->nandp, blk, page, (uint8_t*)&tmp, sizeof(tmp));
  return tmp.session;
}

/**
 * @brief   Calculate total amount of available good blocks
 */
static uint32_t get_total_good(NandRing *ring) {
  uint32_t ret = 0;
  uint32_t current = ring->config->start_blk;

  while (current <= ring->config->end_blk) {
    if (! nandIsBad(ring->config->nandp, current)) {
      ret++;
    }
    current++;
  }
  return ret;
}

/**
 * @brief crc algorithm for spare area
 * @param data
 * @param len
 * @return
 */
static uint8_t nand_ring_crc8(const uint8_t *data, size_t len) {
  uint8_t ret = 0;

  for(size_t i=0; i<len; i++) {
    ret += data[i];
  }

  return ret;
}

/**
 * @brief   Unpack spare data to header struct
 */
static void spare2header(const uint8_t *buf, NandPageHeader *header) {
  uint8_t crc;

  memcpy(header, buf, sizeof(*header));
  // verification of CRC here. If incorrect than overwrite ID field with 0xFF
  crc = nand_ring_crc8(buf, sizeof(*header) - sizeof(header->spare_crc));
  if (crc != header->spare_crc) {
    header->id = PAGE_ID_ERASED;
  }
}

/**
 *
 */
static void header2spare(uint8_t *buf, const NandPageHeader *header) {
  memset(buf, 0xFF, sizeof(*header));
  memcpy(buf, header, sizeof(*header));
}

/*
 ******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************
 */

/**
 * @brief nandRingObjectInit
 * @param ring
 */
void nandRingObjectInit(NandRing *ring) {

  osalDbgCheck(NULL != ring);

  ring->config = NULL;
  ring->state = NAND_RING_UNINIT;
  /* other fields will be initialized in start() method */
}

/**
 * @brief nandRingStart
 * @param ring
 * @param config
 */
void nandRingStart(NandRing *ring, const NandRingConfig *config) {

  osalDbgCheck((NULL != ring) && (NULL != config) && (NULL != config->nandp));
  osalDbgAssert(config->end_blk > config->start_blk + MIN_RING_BLOCKS,
                "Such small storage size is pointless");
  osalDbgAssert(NAND_READY == config->nandp->state,
                "NAND must be started externally");
  osalDbgAssert(sizeof(NandPageHeader) <= config->nandp->config->page_spare_size,
                "Not enough room in spare area");

  ring->cur_blk = config->start_blk;
  ring->cur_page = 0;
  ring->cur_id = 0;
  ring->cur_session = 0;
  ring->utc_correction = 0;
  ring->total_good_blk = 0;
  ring->config = config;
  ring->state = NAND_RING_IDLE;
}

/**
 *
 */
bool nandRingMkfs(NandRing *ring) {
  // only unmounted flash may be formatted
  osalDbgCheck(NAND_RING_IDLE == ring->state);

  uint32_t current = ring->config->start_blk;
  const uint32_t end_blk = ring->config->end_blk;
  NANDDriver *nandp = ring->config->nandp;

  while (current <= end_blk) {
    if (! nandIsBad(nandp, current)) {
      nandErase(nandp, current);
      // TODO: check status and set bad mark if needed
    }
    current++;
  }

  ring->total_good_blk = get_total_good(ring);
  if (ring->total_good_blk < MIN_GOOD_BLOCKS)
    return OSAL_FAILED;
  else
    return OSAL_SUCCESS;
}

/**
 *
 */
bool nandRingMount(NandRing *ring) {

  osalDbgCheck(NULL != ring);
  osalDbgCheck(NAND_RING_IDLE == ring->state);

  uint64_t last_id = 0;
  uint32_t last_blk = 0;
  uint32_t last_page = 0;
  NANDDriver *nandp = ring->config->nandp;
  const size_t ppb = nandp->config->pages_per_block;
  const size_t pss = nandp->config->page_spare_size;
  uint8_t sparebuf[pss];
  NandPageHeader header;

  ring->total_good_blk = get_total_good(ring);
  if (ring->total_good_blk < MIN_GOOD_BLOCKS) {
    return OSAL_FAILED;
  }

  ring->cur_blk = next_good(ring, ring->config->end_blk); /* find first good block */
  last_blk = ring->cur_blk;

  /* Find last written block. Presume it has biggest ID. */
  while (next_good(ring, ring->cur_blk) > ring->cur_blk) {
    nandReadPageSpare(nandp, ring->cur_blk, 0, sparebuf, pss);
    spare2header(sparebuf, &header);
    if ((PAGE_ID_ERASED != header.id) && (header.id >= last_id)) {
      last_blk = ring->cur_blk;
      last_id = header.id;
    }
    ring->cur_blk = next_good(ring, ring->cur_blk);
  }

  /* Find last written page. Presume it has biggest ID. */
  last_page = 0;
  for (size_t p=0; p<ppb; p++) {
    nandReadPageSpare(nandp, last_blk, p, sparebuf, pss);
    spare2header(sparebuf, &header);
    if ((PAGE_ID_ERASED != header.id) && (header.id >= last_id)) {
      last_page = p;
      last_id = header.id;
    }
  }

  /* "recover" latest data */
  ring->cur_blk = next_good(ring, last_blk);
  nandErase(nandp, ring->cur_blk);
  // TODO: check erase status and set bad flag if needed
  for (size_t p=0; p<last_page; p++) {
    nandReadPageWhole(nandp, last_blk, p, scratchpad, SCRATCHPAD_SIZE);
    nandWritePageWhole(nandp, ring->cur_blk, p, scratchpad, SCRATCHPAD_SIZE);
    // TODO: check returned written status
    // TODO: check ECC
    // TODO: use internal buffers for rewriting data
  }
  nandErase(nandp, last_blk);
  // TODO: check erase status and set bad flag if needed
  for (size_t p=0; p<last_page; p++) {
    nandReadPageWhole(nandp, ring->cur_blk, p, scratchpad, SCRATCHPAD_SIZE);
    nandWritePageWhole(nandp, last_blk, p, scratchpad, SCRATCHPAD_SIZE);
    // TODO: check returned written status
    // TODO: check ECC
    // TODO: use internal buffers for rewriting data
  }

  /* set "pointers" to the begining of the free area */
  ring->cur_session = read_session_num(ring, last_blk, last_page);
  ring->cur_session++;
  ring->cur_id = last_id + 1;
  ring->cur_blk = last_blk;
  ring->cur_page = last_page + 1;
  osalDbgAssert(ring->cur_page <= ppb, "Overflow");
  if (ring->cur_page == ppb) {
    ring->cur_page = 0;
    ring->cur_blk = next_good(ring, ring->cur_blk);
  }

  ring->state = NAND_RING_MOUNTED;
  return OSAL_SUCCESS;
}

/**
 * @brief NandRing::umount
 */
void nandRingUmount(NandRing *ring) {
  ring->state = NAND_RING_IDLE;
}

/**
 * @brief NandRing::umount
 */
void nandRingStop(NandRing *ring) {
  ring->state = NAND_RING_STOP;
  ring->config = NULL;
}

/**
 * @brief   Flush data to NAND page and seal it using spare area.
 * @note    Buffer must be the same size as page.
 * @note    This function able to write only single whole page at time.
 */
void nandRingWritePage(NandRing *ring, const uint8_t *data) {

  osalDbgCheck((NULL != data) && (NULL != ring));
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);

  NANDDriver *nandp = ring->config->nandp;
  const size_t ppb = nandp->config->pages_per_block;
  const size_t pss = nandp->config->page_spare_size;
  const size_t pds = nandp->config->page_data_size;
  uint8_t sparebuf[pss];
  NandPageHeader header;

  nandWritePageData(nandp, ring->cur_blk, ring->cur_page, data, pds, &header.page_ecc);
  // TODO: check written status and set bad mark

  header.id = ring->cur_id;
  header.utc_correction = ring->utc_correction;
  header.time_boot_uS = timebootU64();
  header.spare_crc = nand_ring_crc8((uint8_t *)&header,
                                    sizeof(header) - sizeof(header.spare_crc));

  header2spare(sparebuf, &header);
  nandWritePageSpare(nandp, ring->cur_blk, ring->cur_page, sparebuf, pss);
  // TODO: check written status and set bad mark

  /* prepare next iteration */
  ring->cur_id++;
  ring->cur_page++;
  osalDbgAssert(ring->cur_page <= ppb, "Overflow");
  if (ring->cur_page == ppb) {
    ring->cur_page = 0;
    ring->cur_blk = next_good(ring, ring->cur_blk);
  }
}

/**
 * @brief nandRingSetUtcCorrection
 * @param correction
 */
void nandRingSetUtcCorrection(uint32_t correction) {
  (void)correction;
  osalSysHalt("Unrealized yet");
}

/**
 * @brief nandRingSearchSessions
 * @param result
 * @param max_sessions
 * @return
 */
size_t nandRingSearchSessions(RingSession *result, size_t max_sessions) {
  (void)result;
  (void)max_sessions;
  osalSysHalt("Unrealized yet");
  return 0;
}

