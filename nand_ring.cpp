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
 */

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */
#define PAGE_ID_ERASED            0xFFFFFFFFFFFFFFFF
#define WORKING_AREA_SIZE         (2048 + 64) /* TODO: delete this hardcode */
#define COMPILE_TIME_UTC          1400000000

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
static void spare2header(const uint8_t *buf, SpareFormat *header) {
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
static void header2spare(uint8_t *buf, const SpareFormat *header) {
  memset(buf, 0xFF, sizeof(*header));
  memcpy(buf, header, sizeof(*header));
}

/**
 * @brief NandRing::flush
 * @param data
 */
void NandRing::flush(const uint8_t *data) {
  const size_t ppb = this->nandp->config->pages_per_block;
  const size_t pss = this->nandp->config->page_spare_size;
  SpareFormat header;

  osalDbgCheck(nullptr != data);

  nandWritePageData(nandp, current_blk, current_page, data, 2048, &header.page_ecc);
  // TODO: check written status
  // FIXME: delete hardcoded page size

  header.id = this->current_id;
  header.utc_correction = this->utc_correction;
  header.time_boot_uS = ST2US(chVTGetSystemTimeX());
  header.spare_crc = nand_ring_crc8((uint8_t *)&header,
                                    sizeof(header) - sizeof(header.spare_crc));

  header2spare(sparebuf, &header);
  nandWritePageSpare(nandp, current_blk, current_page, sparebuf, pss);
  // TODO: check written status
  // FIXME: delete hardcoded page size

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
    self->mailbox.fetch(&data, MS2ST(200));
    self->flush(data);
    data = nullptr;
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
  utc_correction(COMPILE_TIME_UTC),
  start_blk(start_blk),
  end_blk(end_blk),
  nandp(nandp),
  sparebuf((uint8_t *)chCoreAlloc(nandp->config->page_spare_size)),
  worker(nullptr)
{
  osalDbgAssert(nullptr != sparebuf, "Can not allocate memory");
  osalDbgAssert(sizeof(SpareFormat) <= nandp->config->page_spare_size,
                "Not enough room in spare area");
}

/**
 *
 */
bool NandRing::mount(void) {
  uint64_t maxid = 0;
  uint32_t final_blk = 0;
  uint32_t final_page = 0;
  const size_t ppb = this->nandp->config->pages_per_block;
  const size_t pss = this->nandp->config->page_spare_size;
  SpareFormat header;

  current_blk = next_good(end_blk); /* find first good block */
  final_blk = current_blk;

  /* Find final written block. Presume it has biggest ID. */
  while (next_good(current_blk) > current_blk) {
    nandReadPageSpare(nandp, current_blk, 0, sparebuf, pss);
    spare2header(sparebuf, &header);
    if ((PAGE_ID_ERASED != header.id) && (header.id >= maxid)) {
      final_blk = current_blk;
      maxid = header.id;
    }
    current_blk = next_good(current_blk);
  }

  /* Find final written page. Presume it has biggest ID. */
  final_page = 0;
  for (size_t p=0; p<ppb; p++) {
    nandReadPageSpare(nandp, final_blk, p, sparebuf, pss);
    spare2header(sparebuf, &header);
    if ((PAGE_ID_ERASED != header.id) && (header.id >= maxid)) {
      final_page = p;
      maxid = header.id;
    }
  }

  /* "recover" latest data */
  current_blk = next_good(final_blk);
  nandErase(nandp, current_blk);
  // TODO: check erase status and set bad flag if needed
  for (size_t p=0; p<final_page; p++) {
    nandReadPageWhole(nandp, final_blk, p, working_area, WORKING_AREA_SIZE);
    nandWritePageWhole(nandp, current_blk, p, working_area, WORKING_AREA_SIZE);
    // TODO: check returned written status
    // TODO: use internal buffers for rewriting data
  }
  nandErase(nandp, final_blk);
  // TODO: check erase status and set bad flag if needed
  for (size_t p=0; p<final_page; p++) {
    nandReadPageWhole(nandp, current_blk, p, working_area, WORKING_AREA_SIZE);
    nandWritePageWhole(nandp, final_blk, p, working_area, WORKING_AREA_SIZE);
    // TODO: check returned written status
    // TODO: use internal buffers for rewriting data
  }

  /* set "pointers" to last free area */
  current_id = maxid + 1;
  current_blk = final_blk;
  current_page = final_page + 1;
  osalDbgAssert(current_page <= ppb, "Overflow");
  if (current_page == ppb) {
    current_page = 0;
    current_blk = next_good(current_blk);
  }

  /* launch async data flusher */
  this->worker = chThdCreateStatic(NandWorkerThreadWA, sizeof(NandWorkerThreadWA),
                                   NORMALPRIO, NandWorker, this);
  osalDbgAssert(nullptr != this->worker, "Can not allocate memroy");

  return OSAL_SUCCESS;
}

/**
 *
 */
bool NandRing::append(const uint8_t *data, size_t len) {
  (void)header2spare;

  // TODO: check available space in buffer and return FAILED if not enough.

  uint8_t *ptr = multibuf.append(data, len);
  if (nullptr != ptr) {
    this->flush(ptr);
  }

  return OSAL_SUCCESS;
}





