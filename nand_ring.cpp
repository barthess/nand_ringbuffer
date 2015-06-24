#include "ch.hpp"
#include "hal.h"
#include "string.h"

#include "nand_ring.hpp"

using namespace chibios_rt;

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
#define SCRATCHPAD_SIZE           (2048 + 64) /* TODO: delete this hardcode */
#define COMPILE_TIME_UTC          1400000000
#define MIN_RING_BLOCKS           16 // minimal meaningful storage size
#define MIN_GOOD_BLOCKS           4 // minimal allowable good blocks count

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
static systime_t prev_time = 0;
static size_t wrap_cnt = 0;
static const uint64_t overlap_size = (uint64_t)1 << (sizeof(systime_t) * 8);
static uint64_t get_time_boot_us(void) {
  systime_t now = chVTGetSystemTimeX();

  if (prev_time > now) {
    wrap_cnt++;
  }
  prev_time = now;

  uint64_t tmp = overlap_size * wrap_cnt + now;
  return (tmp * 1000000 + CH_CFG_ST_FREQUENCY - 1) / CH_CFG_ST_FREQUENCY;
}

/**
 *
 */
uint32_t NandRing::next_good(uint32_t current) {

  while (true) {
    current++;
    if (current > end_blk)
      current = start_blk;
    if (! nandIsBad(nandp, current))
      return current;
  }
}

/**
 * @brief NandRing::get_session_num
 * @param blk
 * @param page
 * @return
 */
uint8_t NandRing::read_session_num(uint32_t blk, uint32_t page) {
  NandPageHeader tmp;
  nandReadPageSpare(nandp, blk, page, (uint8_t*)&tmp, sizeof(tmp));
  return tmp.session;
}

/**
 * @brief   Calculate total amount of available good blocks
 */
uint32_t NandRing::get_total_good(void) {
  uint32_t ret = 0;
  uint32_t current = this->start_blk;

  while (current <= end_blk) {
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
 * @brief   Unpack spare data to header struct
 */
static void spare2header(const uint8_t *buf, NandPageHeader *header) {
  uint8_t crc;

  memcpy(header, buf, sizeof(*header));
  // verification of CRC here. If incorrect than overwrite ID field with 0xFF
  crc = nand_ring_crc8(buf, sizeof(*header) - sizeof(header->spare_crc));
  if (crc != header->spare_crc)
    memset(&header->id, 0xFF, sizeof(header->id));
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
 *
 */
NandRing::NandRing(uint32_t start_blk, uint32_t end_blk) :
  current_blk(start_blk),
  current_page(0),
  current_id(0),
  current_session(0),
  utc_correction(COMPILE_TIME_UTC),
  start_blk(start_blk),
  end_blk(end_blk),
  nandp(nullptr),
  state(nand_ring_state_t::UNINIT),
  sparebuf(nullptr),
  total_good_blk(0)
{
  osalDbgAssert(end_blk > start_blk+MIN_RING_BLOCKS,
                "Such small storage size is pointless");
}

/**
 * @brief NandRing::~NandRing
 */
NandRing::~NandRing(void) {
  osalSysHalt("Destruction forbidden");
}

/**
 * @pre  NAND must be configured and started externally
 */
void NandRing::start(NANDDriver *nandp, uint8_t *sparebuf) {
  osalDbgCheck((NAND_UNINIT != nandp->state) && (NAND_STOP != nandp->state));

  this->nandp = nandp;
  this->sparebuf = sparebuf;
  osalDbgAssert(nullptr != sparebuf, "Can not allocate memory");
  osalDbgAssert(sizeof(NandPageHeader) <= nandp->config->page_spare_size,
                "Not enough room in spare area");
  this->state = nand_ring_state_t::IDLE;
}

/**
 *
 */
bool NandRing::mkfs(void) {
  // only unmounted flash may be formatted
  osalDbgCheck(nand_ring_state_t::IDLE == state);

  uint32_t current = this->start_blk;

  while (current <= end_blk) {
    if (! nandIsBad(nandp, current)) {
      nandErase(nandp, current);
      // TODO: check status and set bad mark if needed
    }
    current++;
  }

  total_good_blk = get_total_good();
  if (total_good_blk < MIN_GOOD_BLOCKS)
    return OSAL_FAILED;
  else
    return OSAL_SUCCESS;
}

/**
 *
 */
bool NandRing::mount(void) {
  uint64_t last_id = 0;
  uint32_t last_blk = 0;
  uint32_t last_page = 0;
  const size_t ppb = this->nandp->config->pages_per_block;
  const size_t pss = this->nandp->config->page_spare_size;
  NandPageHeader header;

  osalDbgCheck(nand_ring_state_t::IDLE == state);

  total_good_blk = get_total_good();
  if (total_good_blk < MIN_GOOD_BLOCKS)
    return OSAL_FAILED;

  current_blk = next_good(end_blk); /* find first good block */
  last_blk = current_blk;

  /* Find last written block. Presume it has biggest ID. */
  while (next_good(current_blk) > current_blk) {
    nandReadPageSpare(nandp, current_blk, 0, sparebuf, pss);
    spare2header(sparebuf, &header);
    if ((PAGE_ID_ERASED != header.id) && (header.id >= last_id)) {
      last_blk = current_blk;
      last_id = header.id;
    }
    current_blk = next_good(current_blk);
  }

  /* Find final written page. Presume it has biggest ID. */
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
  current_blk = next_good(last_blk);
  nandErase(nandp, current_blk);
  // TODO: check erase status and set bad flag if needed
  for (size_t p=0; p<last_page; p++) {
    nandReadPageWhole(nandp, last_blk, p, scratchpad, SCRATCHPAD_SIZE);
    nandWritePageWhole(nandp, current_blk, p, scratchpad, SCRATCHPAD_SIZE);
    // TODO: check returned written status
    // TODO: check ECC
    // TODO: use internal buffers for rewriting data
  }
  nandErase(nandp, last_blk);
  // TODO: check erase status and set bad flag if needed
  for (size_t p=0; p<last_page; p++) {
    nandReadPageWhole(nandp, current_blk, p, scratchpad, SCRATCHPAD_SIZE);
    nandWritePageWhole(nandp, last_blk, p, scratchpad, SCRATCHPAD_SIZE);
    // TODO: check returned written status
    // TODO: check ECC
    // TODO: use internal buffers for rewriting data
  }

  /* set "pointers" to the begining of the free area */
  current_session = read_session_num(last_blk, last_page);
  current_session++;
  current_id = last_id + 1;
  current_blk = last_blk;
  current_page = last_page + 1;
  osalDbgAssert(current_page <= ppb, "Overflow");
  if (current_page == ppb) {
    current_page = 0;
    current_blk = next_good(current_blk);
  }

  state = nand_ring_state_t::MOUNTED;
  return OSAL_SUCCESS;
}

/**
 * @brief NandRing::umount
 */
void NandRing::umount(void) {
  state = nand_ring_state_t::IDLE;
}

/**
 * @brief NandRing::umount
 */
void NandRing::stop(void) {
  state = nand_ring_state_t::UNINIT;
}

/**
 * @brief   Flush data to NAND page and seal it using spare area.
 * @note    Buffer must be the same size as page.
 * @note    This function can write single whole page at time.
 */
void NandRing::writePage(const uint8_t *data) {
  const size_t ppb = this->nandp->config->pages_per_block;
  const size_t pss = this->nandp->config->page_spare_size;
  const size_t pds = this->nandp->config->page_data_size;
  NandPageHeader header;

  osalDbgCheck(nullptr != data);
  osalDbgCheck(nand_ring_state_t::MOUNTED == state);

  nandWritePageData(nandp, current_blk, current_page, data, pds, &header.page_ecc);
  // TODO: check written status and set bad mark

  header.id = this->current_id;
  header.utc_correction = this->utc_correction;
  header.time_boot_uS = get_time_boot_us();
  header.spare_crc = nand_ring_crc8((uint8_t *)&header,
                                    sizeof(header) - sizeof(header.spare_crc));

  header2spare(sparebuf, &header);
  nandWritePageSpare(nandp, current_blk, current_page, sparebuf, pss);
  // TODO: check written status and set bad mark

  /* prepare next iteration */
  current_id++;
  current_page++;
  osalDbgAssert(current_page <= ppb, "Overflow");
  if (current_page == ppb) {
    current_page = 0;
    current_blk = next_good(current_blk);
  }
}
