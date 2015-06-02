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
#define WORKING_AREA_SIZE         (2048 + 64) /* TODO: delete this hardcode */
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
static uint8_t working_area[WORKING_AREA_SIZE];

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
    if (current > end)
      current = start;
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
  uint32_t current = this->start;

  while (current <= end) {
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

/**
 * @brief   Flush data block to NAND page and seal it using spare area.
 */
void NandRing::flush(const uint8_t *data) {
  const size_t ppb = this->nandp->config->pages_per_block;
  const size_t pss = this->nandp->config->page_spare_size;
  const size_t pds = this->nandp->config->page_data_size;
  NandPageHeader header;

  osalDbgCheck(nullptr != data);

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

/**
 *
 */
static THD_WORKING_AREA(NandWorkerThreadWA, 320);
THD_FUNCTION(NandWorker, arg) {
  chRegSetThreadName("NandWorker");
  NandRing *self = static_cast<NandRing *>(arg);
  uint8_t *data = nullptr;

  while (!chThdShouldTerminateX()) {
    if (MSG_OK == self->mailbox.fetch(&data, MS2ST(200))){
      self->flush(data);
      self->multibuf.free(data);
      data = nullptr;
    }
  }

  chThdExit(MSG_OK);
}

/*
 ******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************
 */
/**
 *
 */
NandRing::NandRing(NANDDriver *nandp, uint32_t start_blk, uint32_t end_blk) :
  current_blk(start_blk),
  current_page(0),
  current_id(0),
  current_session(0),
  utc_correction(COMPILE_TIME_UTC),
  start(start_blk),
  end(end_blk),
  nandp(nandp),
  sparebuf((uint8_t *)chCoreAlloc(nandp->config->page_spare_size)),
  worker(nullptr),
  ready(false),
  total_good_blk(0)
{
  osalDbgAssert(end_blk > start_blk+MIN_RING_BLOCKS,
                "Such small storage size is pointless");
  osalDbgAssert(nullptr != sparebuf, "Can not allocate memory");
  osalDbgAssert(sizeof(NandPageHeader) <= nandp->config->page_spare_size,
                "Not enough room in spare area");
}

/**
 * @brief NandRing::~NandRing
 */
NandRing::~NandRing(void) {
  osalSysHalt("Destruction forbidden");
}

/**
 *
 */
bool NandRing::mkfs(void) {
  // only unmounted flash able to be formatted
  osalDbgCheck(false == ready);

  uint32_t current = this->start;

  while (current <= end) {
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

  total_good_blk = get_total_good();
  if (total_good_blk < MIN_GOOD_BLOCKS)
    return OSAL_FAILED;

  current_blk = next_good(end); /* find first good block */
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
    nandReadPageWhole(nandp, last_blk, p, working_area, WORKING_AREA_SIZE);
    nandWritePageWhole(nandp, current_blk, p, working_area, WORKING_AREA_SIZE);
    // TODO: check returned written status
    // TODO: check ECC
    // TODO: use internal buffers for rewriting data
  }
  nandErase(nandp, last_blk);
  // TODO: check erase status and set bad flag if needed
  for (size_t p=0; p<last_page; p++) {
    nandReadPageWhole(nandp, current_blk, p, working_area, WORKING_AREA_SIZE);
    nandWritePageWhole(nandp, last_blk, p, working_area, WORKING_AREA_SIZE);
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

  /* launch async data worker */
  this->worker = chThdCreateStatic(NandWorkerThreadWA, sizeof(NandWorkerThreadWA),
                                   NORMALPRIO, NandWorker, this);
  osalDbgAssert(nullptr != this->worker, "Can not allocate memroy");

  ready = true;
  return OSAL_SUCCESS;
}

/**
 *
 */
size_t NandRing::append(const uint8_t *data, size_t len) {
  size_t written = 0;

  osalDbgCheck(this->ready);

  uint8_t *ptr = multibuf.append(data, len, &written);

  if (nullptr != ptr) {
    // There is not check for post status because mailbox has exactly the
    // same slots count as multibuffer has.
    mailbox.post(ptr, TIME_IMMEDIATE);
  }

  return written;
}
