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

/*
 ******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************
 */

/**
 * @brief   Erase range of blocks.
 * @param   nandp
 * @param   start
 * @param   len
 * @return  number of newly detected bad blocks
 */
uint32_t nandEraseRange(NANDDriver *nandp, uint32_t start, uint32_t len) {

  uint32_t ret = 0;

  osalDbgCheck(len > 0);

  for (size_t b=start; b<len+start; b++) {
    if (! nandIsBad(nandp, b)) {
      const uint8_t status = nandErase(nandp, b);
      if (status & NAND_STATUS_FAILED) {
        nandMarkBad(nandp, b);
        ret++;
      }
    }
  }

  return ret;
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





