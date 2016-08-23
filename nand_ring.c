#include <string.h>

#include "ch.h"
#include "hal.h"

#include "nand_ring.h"
#include "timeboot_u64.h"
#include "soft_crc.h"
#include "libnand.h"

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

#define PAGE_ID_WASTED            (uint64_t)0x0  /* page erased or CRC broken */
#define PAGE_ID_FIRST             (uint64_t)0x1

#define BLOCK_NOT_FOUND           0xFFFFFFFF
#define LAST_PAGE_NOT_FOUND       0xFFFFFFFF

#define MIN_RING_SIZE             32

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
static uint32_t get_total_good(const NandRing *ring) {

  osalDbgCheck(NULL != ring->config);

  uint32_t ret = 0;
  NANDDriver *nandp = ring->config->nandp;
  const uint32_t start = ring->config->start_blk;
  const uint32_t len = ring->config->len;

  for (size_t b=0; b<len; b++) {
    if (! nandIsBad(nandp, start+b)) {
      ret++;
    }
  }

  return ret;
}

/**
 *
 */
static uint32_t next_good(const NandRing *ring, uint32_t current) {

  const uint32_t len   = ring->config->len;
  const uint32_t start = ring->config->start_blk;
  NANDDriver *nandp    = ring->config->nandp;
  uint32_t b = current;

  do {
    b++;
    if (b == (start + len)) {
      b = start;
    }
    if (! nandIsBad(nandp, b)) {
      return b;
    }
  } while (b != current); /* search wrapped without success */

  const uint32_t total_good = get_total_good(ring);
  osalDbgCheck(total_good <= 1); /* false negative result */
  return BLOCK_NOT_FOUND;
}

/**
 * @brief erase_next
 * @param ring
 * @return
 */
static uint32_t erase_next(NandRing *ring, uint32_t cur_blk) {

  NANDDriver *nandp = ring->config->nandp;
  uint8_t status = NAND_STATUS_FAILED;
  uint32_t blk = BLOCK_NOT_FOUND;

  do {
    blk = next_good(ring, cur_blk);
    if (BLOCK_NOT_FOUND == blk) {
      return BLOCK_NOT_FOUND;
    }
    status = nandErase(nandp, blk);
    if (nandFailed(status)) {
      ring->dbg.erase_failed++;
      ring->dbg.new_badblocks++;
      nandMarkBad(nandp, blk);
    }
  } while (nandFailed(status));

  return blk;
}

/**
 *
 */
static uint32_t calc_spare_crc(const NandPageHeader *header) {

  const size_t len = sizeof(NandPageHeader) - sizeof(header->spare_crc);
  return softcrc32((const uint8_t *)header, len, 0xFFFFFFFF);
}

/**
 * @brief check_header_crc
 * @return
 */
static bool header_crc_valid(const NandPageHeader *header) {

  const uint32_t crc = calc_spare_crc(header);
  return header->spare_crc == crc;
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
  NandPageHeader header;

  nandReadPageSpare(nandp, blk, page, (uint8_t *)&header, sizeof(NandPageHeader));

  if (! header_crc_valid(&header)) {
    return PAGE_ID_WASTED;
  }
  else {
    return header.id;
  }
}

/**
 * @brief get_last_blk
 * @param ring
 * @return
 */
static uint32_t get_last_blk(const NandRing *ring) {
  return ring->config->start_blk + ring->config->len - 1;
}

/**
 * @brief   Find last written block using brute force method starting
 *          from the first block of the ring
 * @return
 */
static uint32_t last_written_block(const NandRing *ring) {

  /* very first block of ring */
  const uint32_t first = next_good(ring, get_last_blk(ring));
  if (BLOCK_NOT_FOUND == first) {
    return BLOCK_NOT_FOUND;
  }
  uint32_t last_blk = BLOCK_NOT_FOUND;
  uint64_t last_id  = PAGE_ID_FIRST;

  /* iterate over good blocks until block number wraps */
  uint32_t b = first;
  do {
    const uint64_t id = read_page_id(ring, b, 0);
    if (id >= last_id) {
      last_blk = b;
      last_id  = id;
    }
    b = next_good(ring, b);
    if (BLOCK_NOT_FOUND == b) {
      return BLOCK_NOT_FOUND;
    }
  } while (b > first);

  return last_blk;
}

/**
 * @brief   Find last written page in the last written block
 * @param   ring
 * @param   last_blk
 * @return
 */
static uint32_t last_written_page(const NandRing *ring, uint32_t last_blk) {

  osalDbgCheck(BLOCK_NOT_FOUND != last_blk);

  uint64_t last_id = PAGE_ID_FIRST;
  uint32_t last_page = LAST_PAGE_NOT_FOUND;
  const size_t ppb = ring->config->nandp->config->pages_per_block;

  for (size_t page=0; page<ppb; page++) {
    const uint64_t id = read_page_id(ring, last_blk, page);
    if (id >= last_id) {
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
 * @brief wa_size
 * @param nandp
 * @return
 */
static uint32_t wa_size(const NANDDriver *nandp) {
  return nandp->config->page_data_size + nandp->config->page_spare_size;
}

/**
 * @brief Overwrites erased pages in last block.
 * @param ring
 * @param last_blk
 * @param last_page
 * @return
 */
static uint32_t close_prev_session(NandRing *ring, uint32_t last_blk,
                                   uint32_t last_page) {

  NANDDriver *nandp = ring->config->nandp;
  const size_t ppb = nandp->config->pages_per_block;
  const size_t pds = nandp->config->page_data_size;

  if (last_page != (ppb - 1)) {
    memset(ring->wa, 0, wa_size(nandp));
    ring->wa[pds]   = 0xFF;
    ring->wa[pds+1] = 0xFF;

    for (size_t page=last_page; page<ppb; page++) {
      const uint8_t status = nandWritePageWhole(nandp, last_blk, page,
                                                ring->wa, wa_size(nandp));
      if (nandFailed(status)) {
        ring->dbg.new_badblocks++;
        nandMarkBad(nandp, last_blk);
      }
    }
  }

  return erase_next(ring, last_blk);
}

/**
 *
 */
static uint32_t mkfs(NandRing *ring) {

  return erase_next(ring, get_last_blk(ring));
}

/**
 * @brief   Move data from failed block to new one and set bad mark in old.
 * @retval  New block number.
 */
static uint32_t block_data_rescue(NandRing *ring, uint32_t failed_blk,
                                  uint32_t failed_page) {

  NANDDriver *nandp = ring->config->nandp;
  uint32_t target_blk;
  uint8_t status = NAND_STATUS_FAILED;

  if (failed_page > 0) {
    RETRY:
    target_blk = erase_next(ring, ring->cur_blk);
    if (BLOCK_NOT_FOUND == target_blk) {
      return BLOCK_NOT_FOUND;
    }
    status = nandDataMove(nandp, failed_blk, target_blk, failed_page, ring->wa);
    ring->dbg.data_rescue++;
    if (nandFailed(status)) {
      nandMarkBad(nandp, target_blk);
      ring->dbg.new_badblocks++;
      goto RETRY;
    }
  }
  else {
    target_blk = erase_next(ring, ring->cur_blk);
    if (BLOCK_NOT_FOUND == target_blk) {
      return BLOCK_NOT_FOUND;
    }
  }

  return target_blk;
}

/**
 * @brief seal_header
 * @param ring
 * @param header
 * @param page_ecc
 * @param actually_written
 */
static void fill_header(const NandRing *ring, NandPageHeader *header,
                        uint32_t page_ecc, uint32_t actually_written) {

  header->page_ecc       = page_ecc;
  header->bad_mark       = 0xFFFF;
  header->id             = ring->cur_id;
  header->utc_correction = 0;
  header->time_boot_us   = timebootU64();
  header->session        = 0;
  header->written        = actually_written;

  /* must be at the very end of operation */
  header->spare_crc      = calc_spare_crc(header);
}

/**
 * @brief reset_debug
 */
static void reset_debug(NandRing *ring) {
  memset(&ring->dbg, 0, sizeof(nand_ring_debug_t));
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

  ring->wa = NULL;
  ring->config = NULL;
  ring->state = NAND_RING_UNINIT;

  reset_debug(ring);

  /* other fields will be initialized during start() */
}

/**
 * @brief nandRingStart
 * @param ring
 * @param config
 * @param working_area must be big enough to store page data + page spare
 */
void nandRingStart(NandRing *ring, const NandRingConfig *config, uint8_t *working_area) {

  osalDbgCheck((NULL != ring) && (NULL != config) && (NULL != config->nandp));
  osalDbgAssert(NAND_READY == config->nandp->state,
                "NAND must be started externally");
  osalDbgAssert((config->start_blk + config->len) <= config->nandp->config->blocks,
                "NAND overflow");
  osalDbgCheck(config->len >= MIN_RING_SIZE);
  osalDbgAssert(sizeof(NandPageHeader) <= config->nandp->config->page_spare_size,
                "Not enough room in spare area");

  ring->config = config;
  ring->state = NAND_RING_IDLE;
  ring->wa = working_area;
  /* other fields will be initialized during mount() */
}

/**
 *
 */
bool nandRingMount(NandRing *ring) {

  osalDbgCheck(NULL != ring);
  osalDbgCheck(NAND_RING_IDLE == ring->state);

  if (get_total_good(ring) < (ring->config->len / 2)) {
    return OSAL_FAILED;
  }

  uint32_t last_blk  = last_written_block(ring);
  if (BLOCK_NOT_FOUND == last_blk) {
    ring->cur_blk = mkfs(ring);
    ring->cur_page = 0;
    ring->cur_id = PAGE_ID_FIRST;
  }
  else {
    uint32_t last_page = last_written_page(ring, last_blk);
    uint64_t last_id   = read_page_id(ring, last_blk, last_page);

    ring->cur_blk = close_prev_session(ring, last_blk, last_page);
    ring->cur_page = 0;
    ring->cur_id = last_id + 1;
  }

  ring->state = NAND_RING_MOUNTED;
  return OSAL_SUCCESS;
}

/**
 * @brief   Flush data to NAND page and seal it using spare area.
 * @note    Buffer must be the same size as page data.
 * @note    This function able to write only single whole page at time.
 */
bool nandRingWritePage(NandRing *ring, const uint8_t *data) {

  osalDbgCheck((NULL != data) && (NULL != ring));
  if (NAND_RING_NO_SPACE == ring->state)
    return OSAL_FAILED;
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);

  NANDDriver *nandp = ring->config->nandp;
  const size_t ppb = nandp->config->pages_per_block;
  const size_t pds = nandp->config->page_data_size;
  uint32_t page_ecc;
  uint8_t status = NAND_STATUS_FAILED;

  /* write page data */
RETRY:
  status = nandWritePageData(nandp, ring->cur_blk, ring->cur_page,
                             data, pds, &page_ecc);
  if (nandFailed(status)) {
    nandMarkBad(nandp, ring->cur_blk);
    ring->dbg.new_badblocks++;
    ring->dbg.write_data_failed++;
    const uint32_t b = block_data_rescue(ring, ring->cur_blk, ring->cur_page);
    if (BLOCK_NOT_FOUND == b)
      goto NO_SPACE;
    else
      ring->cur_blk = b;
    goto RETRY;
  }

  /* seal page using spare area */
  NandPageHeader header;
  fill_header(ring, &header, page_ecc, pds);
  status = nandWritePageSpare(nandp, ring->cur_blk, ring->cur_page,
                              (uint8_t *)&header, sizeof(NandPageHeader));
  if (nandFailed(status)) {
    nandMarkBad(nandp, ring->cur_blk);
    ring->dbg.new_badblocks++;
    ring->dbg.write_spare_failed++;
    const uint32_t b = block_data_rescue(ring, ring->cur_blk, ring->cur_page);
    if (BLOCK_NOT_FOUND == b)
      goto NO_SPACE;
    else
      ring->cur_blk = b;
    goto RETRY;
  }

  /* prepare next iteration */
  ring->cur_id++;
  ring->cur_page++;
  if (ring->cur_page == ppb) {
    ring->cur_page = 0;
    const uint32_t b = erase_next(ring, ring->cur_blk);
    if (BLOCK_NOT_FOUND == b)
      goto NO_SPACE;
    else
      ring->cur_blk = b;
  }

  return OSAL_SUCCESS;

NO_SPACE:
  ring->state = NAND_RING_NO_SPACE;
  return OSAL_FAILED;
}

/**
 * @brief   Calculate total amount of available good blocks
 */
uint32_t nandRingTotalGood(const NandRing *ring) {

  osalDbgCheck(NULL != ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);

  return get_total_good(ring);
}

/**
 * @brief nandRingWASize
 * @param config
 * @return
 */
uint32_t nandRingWASize(const NANDDriver *nandp) {
  osalDbgCheck(NULL != nandp);
  osalDbgCheck((nandp->state != NAND_STOP) && (nandp->state != NAND_UNINIT));

  return wa_size(nandp);
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

/**
 * @brief nandRingUmount
 * @param ring
 */
void nandRingUmount(NandRing *ring) {

  osalDbgCheck(NULL != ring);
  ring->state = NAND_RING_IDLE;
  reset_debug(ring);
}

/**
 * @brief nandRingStop
 * @param ring
 */
void nandRingStop(NandRing *ring) {

  osalDbgCheck(NULL != ring);
  osalDbgCheck(NAND_RING_IDLE == ring->state);

  ring->state = NAND_RING_STOP;
  ring->config = NULL;
  ring->wa = NULL;
}
