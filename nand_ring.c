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
 * Обновление от 16.08.2016
 *
 * mount()
 * 1) ищем последнюю записанную страницу
 * 1.1) пробегаем все блоки, начиная с первого исправного, читаем из нулевых
 *      страниц id и проверяем его на специальные значения. В случае
 *      битой контрольной суммы id считаем PAGE_ID_WASTED. Если ни одного
 *      валидного блока не обнаружено - возвращаем LAST_BLOCK_NOT_FOUND.
 * 1.2) если найден хотя бы один валидный блок - пробегаем в нем страницы
 *      с нулевой до первой невалидной. Возвращаем номер последней валидной.
 * 1.3) зануляем все оставшиеся страницы блока.
 * 1.4) очищаем следующий исправный блок и устанавливаем на него указатель.
 *
 * mkfs()
 * 1) ищет первый исправный блок, очищает его, возвращает его номер
 */

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */
#define PAGE_ID_ERASED            (uint64_t)0xFFFFFFFFFFFFFFFF
#define PAGE_ID_WASTED            (uint64_t)0x0 /* так же используется в случае сбоя CRC */
#define PAGE_ID_FIRST             (uint64_t)0x1

#define LAST_BLOCK_NOT_FOUND      0xFFFFFFFF
#define LAST_PAGE_NOT_FOUND       0xFFFFFFFF

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
static uint32_t next_good(const NandRing *ring, uint32_t current) {

  const uint32_t end = ring->config->end_blk;
  const uint32_t start = ring->config->start_blk;
  NANDDriver *nandp = ring->config->nandp;

  // TODO: this is infinite loop when no good blocks found
  while (true) {
    current++;
    if (current > end) {
      current = start;
    }
    if (! nandIsBad(nandp, current)) {
      return current;
    }
  }
}

/**
 * @brief   Calculate total amount of available good blocks
 */
static uint32_t get_total_good(NandRing *ring) {
  uint32_t ret = 0;
  uint32_t current = ring->config->start_blk;

  while (current <= ring->config->end_blk) {
    NANDDriver *nandp = ring->config->nandp;
    if (! nandIsBad(nandp, current)) {
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
 * @brief check_header_crc
 * @return
 */
static bool header_crc_valid(const uint8_t *buf) {
  uint8_t crc;
  const NandPageHeader *header = (const NandPageHeader *)buf;

  crc = nand_ring_crc8(buf, sizeof(*header) - sizeof(header->spare_crc));
  if (crc != header->spare_crc) {
    return OSAL_SUCCESS;
  }
  else {
    return OSAL_FAILED;
  }
}

/**
 *
 */
static void header2spare(uint8_t *buf, const NandPageHeader *header) {
  memcpy(buf, header, sizeof(*header));
}

/**
 * @brief read_id
 * @param ring
 * @param blk
 * @param page
 * @return
 */
static uint64_t read_page_id(const NandRing *ring, uint32_t blk, uint32_t page) {

  NANDDriver *nandp = ring->config->nandp;
  const size_t pss = nandp->config->page_spare_size;
  uint8_t sparebuf[pss];

  nandReadPageSpare(nandp, blk, page, sparebuf, pss);
  const NandPageHeader *header = (const NandPageHeader *)sparebuf;

  if ((header->id == PAGE_ID_ERASED) || (header->id == PAGE_ID_WASTED)) {
    return header->id;
  }
  else if (! header_crc_valid(sparebuf)) {
    return PAGE_ID_WASTED;
  }
  else {
    return header->id;
  }
}

/**
 * @brief first_good
 * @param ring
 * @return
 */
static uint32_t first_good(const NandRing *ring) {
  return next_good(ring, ring->config->end_blk);
}

/**
 * @brief   Find last written block using brute force method starting
 *          from the first block of the ring
 * @return
 */
static uint32_t last_written_block(const NandRing *ring) {

  /* very first block of ring */
  uint32_t blk = first_good(ring);
  uint32_t last_blk = LAST_BLOCK_NOT_FOUND;
  uint64_t last_id  = PAGE_ID_FIRST;

  /* iterate blocks until tip value wraps */
  while (next_good(ring, blk) > blk) {
    const uint64_t id = read_page_id(ring, blk, 0);
    if ((PAGE_ID_ERASED != id) && (id >= last_id)) {
      last_blk = blk;
      last_id  = id;
    }
    blk = next_good(ring, blk);
  }

  return last_blk;
}

/**
 * @brief   Find last written page in the last written block
 * @param   ring
 * @param   last_blk
 * @return
 */
static uint32_t last_written_page(const NandRing *ring, uint32_t last_blk) {

  osalDbgCheck(LAST_BLOCK_NOT_FOUND != last_blk);

  uint64_t last_id = PAGE_ID_FIRST;
  uint32_t last_page = LAST_PAGE_NOT_FOUND;
  const size_t ppb = ring->config->nandp->config->pages_per_block;

  for (size_t page=0; page<ppb; page++) {
    const uint64_t id = read_page_id(ring, last_blk, page);
    if ((PAGE_ID_ERASED != id) && (id >= last_id)) {
      last_page = page;
      last_id   = id;
    }
  }

  /* last page search must be called after last block search which must
     find at least one valid page */
  osalDbgCheck(LAST_PAGE_NOT_FOUND != last_page);

  return last_page;
}

/**
 * @brief recover_last_session
 * @param ring
 * @param last_blk
 * @param last_page
 * @return
 */
static uint32_t recover_last_session(const NandRing *ring,
                                     uint32_t last_blk, uint32_t last_page) {

  NANDDriver *nandp = ring->config->nandp;
  const size_t ppb = nandp->config->pages_per_block;
  const size_t ps  = nandp->config->page_data_size;

  if (last_page != (ppb - 1)) {
    memset(scratchpad, 0, SCRATCHPAD_SIZE);
    memset(&scratchpad[ps], 0xFF, 2);
    for (size_t page=last_page; page<ppb; page++) {
      // TODO: check returned written status
      nandWritePageWhole(nandp, last_blk, page, scratchpad, SCRATCHPAD_SIZE);
    }
  }

  uint32_t current_blk = next_good(ring, last_blk);
  // TODO: check erase status and set bad flag if needed
  nandErase(nandp, current_blk);
  return current_blk;
}

/**
 *
 */
static uint32_t mkfs(const NandRing *ring) {

  NANDDriver *nandp = ring->config->nandp;
  uint32_t cur_blk = first_good(ring);

  // TODO: check status and set bad mark if needed
  nandErase(nandp, cur_blk);
  return cur_blk;
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
  osalDbgAssert(NAND_READY == config->nandp->state,
                "NAND must be started externally");
  osalDbgAssert(config->end_blk > config->start_blk + MIN_RING_BLOCKS,
                "Such small storage size is pointless");
  osalDbgAssert(config->end_blk < config->nandp->config->blocks, "NAND overflow");
  osalDbgAssert(sizeof(NandPageHeader) <= config->nandp->config->page_spare_size,
                "Not enough room in spare area");

  ring->cur_blk = config->start_blk;
  ring->cur_page = 0;
  ring->cur_id = 0;
  ring->utc_correction = 0;
  ring->config = config;
  ring->state = NAND_RING_IDLE;
}

/**
 *
 */
bool nandRingMount(NandRing *ring) {

  osalDbgCheck(NULL != ring);
  osalDbgCheck(NAND_RING_IDLE == ring->state);

  if (get_total_good(ring) < MIN_GOOD_BLOCKS) {
    return OSAL_FAILED;
  }

  uint32_t last_blk  = last_written_block(ring);
  if (LAST_BLOCK_NOT_FOUND == last_blk) {
    ring->cur_blk = mkfs(ring);
    ring->cur_page = 0;
    ring->cur_id = PAGE_ID_FIRST;
  }
  else {
    uint32_t last_page = last_written_page(ring, last_blk);
    uint64_t last_id   = read_page_id(ring, last_blk, last_page);

    ring->cur_blk = recover_last_session(ring, last_blk, last_page);
    ring->cur_page = 0;
    ring->cur_id = last_id + 1;
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

  header.bad_mark = 0xFFFF;
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

