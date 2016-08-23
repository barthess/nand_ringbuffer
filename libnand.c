#include <string.h>
#include <stdlib.h>

#include "ch.h"
#include "hal.h"

#include "libnand.h"

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */

#define DEBUG_FAKE_ERROR      TRUE

#if DEBUG_FAKE_ERROR
#define ERROR_CHANCE          32U
#endif

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

#if DEBUG_FAKE_ERROR
static uint32_t error_chance = 0;
#endif

/*
 ******************************************************************************
 ******************************************************************************
 * LOCAL FUNCTIONS
 ******************************************************************************
 ******************************************************************************
 */

#if DEBUG_FAKE_ERROR
/**
 * @brief fake_error
 * @return
 */
static bool fake_error(void) {
  uint32_t rnd = rand();
  return (error_chance > 0) && (0 == rnd % error_chance);
}
#endif

/**
 *
 */
static void fill_pagebuf_rand(void *buf, size_t pagedatasize, size_t sparesize) {

  size_t len = pagedatasize + sparesize;
  osalDbgCheck(0 == len % sizeof(int));
  len /= sizeof(int);

  int *bufi = buf;
  for (size_t i=0; i<len; i++) {
    bufi[i] = rand();
  }

  /* it is important to force bad mark position with 0xFF */
  uint8_t *bufu8 = buf;
  bufu8[pagedatasize]     = 0xFF;
  bufu8[pagedatasize + 1] = 0xFF;
}

/**
 * @brief nand_erase_range
 * @param nandp
 * @param start
 * @param len
 * @param force_bad_erase
 * @return
 */
static uint32_t nand_erase_range(NANDDriver *nandp, uint32_t start,
                                 uint32_t len, bool force_bad_erase) {

  uint32_t ret = 0;

  for (size_t b=start; b<len+start; b++) {
    if (force_bad_erase || (! nandIsBad(nandp, b))) {
      const uint8_t status = nandErase(nandp, b);
      if (nandFailed(status)) {
        nandMarkBad(nandp, b);
        ret++;
      }
    }
  }

  return ret;
}

/*
 ******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************
 */
/**
 * @brief __nandEraseRangeForceDebugOnly
 * @param nandp
 * @param start
 * @param len
 */
uint32_t __nandEraseRangeForce_DebugOnly(NANDDriver *nandp, uint32_t start, uint32_t len) {

  osalDbgCheck(len > 0);
  osalDbgCheck(NULL != nandp);

  return nand_erase_range(nandp, start, len, true);
}

/**
 * @brief   Erase range of blocks.
 * @param   nandp
 * @param   start
 * @param   len
 * @return  number of newly detected bad blocks
 */
uint32_t nandEraseRange(NANDDriver *nandp, uint32_t start, uint32_t len) {

  osalDbgCheck(len > 0);
  osalDbgCheck(NULL != nandp);

  return nand_erase_range(nandp, start, len, false);
}

/**
 * @brief   Fill range of blocks with random data.
 * @note    There is no need to preerase blocks. It will be done under the hood.
 * @param   nandp
 * @param   start
 * @param   len
 * @param   pagebuf must be externally allocated
 * @return  number of newly detected bad blocks.
 */
uint32_t nandFillRandomRange(NANDDriver *nandp, uint32_t start,
                             uint32_t len, void *pagebuf) {

  uint32_t ret;

  osalDbgCheck(len > 0);

  ret = nandEraseRange(nandp, start, len);

  const size_t pds = nandp->config->page_data_size;
  const size_t pss = nandp->config->page_spare_size;
  const size_t ppb = nandp->config->pages_per_block;

  for (size_t blk=start; blk<start+len; blk++) {
    if (! nandIsBad(nandp, blk)) {
      for (size_t page=0; page<ppb; page++) {
        fill_pagebuf_rand(pagebuf, pds, pss);
        const uint8_t status = nandWritePageWhole(nandp, blk, page, pagebuf, pds+pss);
        if (status & NAND_STATUS_FAILED) {
          nandMarkBad(nandp, blk);
          ret++;
          break;
        }
      }
    }
  }

  return ret;
}

/**
 * @brief Move specified number of pages starting from first between blocks.
 * @pre   Target block must be preerased
 * @param nandp
 * @param src_blk
 * @param trgt_blk
 * @param pages
 * @param working_area must be big enough to store page data + page spare
 * @return
 */
uint8_t nandDataMove(NANDDriver *nandp, uint32_t src_blk,
                     uint32_t trgt_blk, uint32_t pages, uint8_t *working_area) {

  const uint32_t len = nandp->config->page_data_size + nandp->config->page_spare_size;
  uint8_t status;

  for (uint32_t p=0; p<pages; p++) {
    nandReadPageWhole(nandp, src_blk, p, working_area, len);
    status = nandWritePageWhole(nandp, trgt_blk, p, working_area, len);
    if (nandFailed(status)) {
      return status;
    }
  }
  return NAND_STATUS_SUCCESS;
}

/**
 * @brief __nandSetErrorChance_DebugOnly
 * @param chance
 * @return
 */
void __nandSetErrorChance_DebugOnly(uint32_t chance) {
#if DEBUG_FAKE_ERROR
  error_chance = chance;
#else
  (void)chance;
#endif
}

/**
 * @brief nandFailed
 * @param status
 * @return
 */
bool nandFailed(uint8_t status) {
#if DEBUG_FAKE_ERROR
  return fake_error() || NAND_STATUS_FAILED == (status & NAND_STATUS_FAILED);
#else
  return NAND_STATUS_FAILED == (status & NAND_STATUS_FAILED);
#endif
}
