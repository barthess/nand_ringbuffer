#include <string.h>
#include <stdlib.h>

#include "ch.h"
#include "hal.h"

#include "libnand.h"
#include "nand_ring.h"
#include "nand_ring_test.h"

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */

#define NAND_TEST_START_BLOCK     2048
#define NAND_TEST_LEN             128

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

static NandRingConfig nandringcfg = {
  NAND_TEST_START_BLOCK,
  NAND_TEST_LEN,
  NULL
};

static NandRing nandring;

static uint16_t badblocks[64];

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
static void get_bad(NANDDriver *nandp) {

  size_t tip = 0;
  memset(badblocks, 0, sizeof(badblocks));
  for (size_t b=0; b<nandp->config->blocks; b++) {
    if (nandIsBad(nandp, b)) {
      badblocks[tip] = b;
      tip++;
    }
  }
}

/**
 * @brief is_sequence_good
 * @param nandp
 * @param start
 * @param end
 * @return
 */
static bool is_sequence_good(NANDDriver *nandp, size_t start, size_t len) {

  for (size_t b=0; b<len; b++) {
    if (nandIsBad(nandp, start+b)) {
      return false;
    }
  }
  return true;
}

/**
 * @brief mount_erased
 * @param ring
 */
static void mount_erased(NandRing *ring) {

  const size_t start = ring->config->start_blk;
  const size_t len   = ring->config->len;
  NANDDriver *nandp  = ring->config->nandp;

  osalDbgCheck(is_sequence_good(nandp, start, len));

  nandEraseRange(nandp, start, len);
  nandRingMount(ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(start == ring->cur_blk);
  osalDbgCheck(0 == ring->cur_page);
  osalDbgCheck(1 == ring->cur_id);
  nandRingUmount(ring);
}

/**
 * @brief mount_trashed
 * @param ringp
 */
static void mount_trashed(NandRing *ring) {

  const size_t start = ring->config->start_blk;
  const size_t len   = ring->config->len;
  NANDDriver *nandp  = ring->config->nandp;
  osalDbgCheck(is_sequence_good(nandp, start, len));

  const size_t pds = nandp->config->page_data_size;
  const size_t pss = nandp->config->page_spare_size;
  void *buf = chHeapAlloc(NULL, pds+pss);
  nandFillRandomRange(nandp, start, len, buf);
  chHeapFree(buf);

  nandRingMount(ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(start == ring->cur_blk);
  osalDbgCheck(0 == ring->cur_page);
  osalDbgCheck(1 == ring->cur_id);
  nandRingUmount(ring);
}

/**
 * @brief write_page_test
 * @param ring
 */
static void write_page_test(NandRing *ring) {

  NANDDriver *nandp  = ring->config->nandp;
  const size_t pds = nandp->config->page_data_size;
  uint8_t *pagebuf = chHeapAlloc(NULL, pds);
  uint64_t id = 1;
  uint32_t blk = ring->config->start_blk;
  uint32_t len = ring->config->len;

  /* this test does not check bad block handling */
  osalDbgCheck(is_sequence_good(nandp, ring->config->start_blk, len));

  /*
   * check single page write
   */
  nandRingMount(ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  nandRingWritePage(ring, pagebuf);
  id++;
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 1);
  osalDbgCheck(ring->cur_id   == id);
  nandRingUmount(ring);

  /*
   * check almost full block write
   */
  blk++;
  nandRingMount(ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  for (size_t i=0; i<nandp->config->pages_per_block - 1; i++){
    nandRingWritePage(ring, pagebuf);
    id++;
  }
  nandRingUmount(ring);

  /*
   * check full block write
   */
  nandRingMount(ring);
  blk++;
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  for (size_t i=0; i<nandp->config->pages_per_block; i++){
    nandRingWritePage(ring, pagebuf);
    id++;
  }
  blk++;
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  nandRingUmount(ring);
  nandRingMount(ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  nandRingUmount(ring);

  /*
   * check full storage write
   */
  nandRingMount(ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  for (; blk < (ring->config->start_blk+len); blk++) {
    for (size_t i=0; i<nandp->config->pages_per_block; i++) {
      nandRingWritePage(ring, pagebuf);
      id++;
    }
  }
  osalDbgCheck(ring->cur_blk  == ring->config->start_blk); /* block number must be wrapped now */
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  blk = ring->config->start_blk;
  nandRingUmount(ring);
  nandRingMount(ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);

  /*
   * check full storage write at single session
   */
  id = 1;
  blk = ring->config->start_blk;
  nandRingUmount(ring);
  mount_erased(ring);
  nandRingMount(ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  for (; blk<(ring->config->start_blk+len); blk++) {
    for (size_t i=0; i<nandp->config->pages_per_block; i++){
      nandRingWritePage(ring, pagebuf);
      id++;
    }
  }
  osalDbgCheck(ring->cur_blk  == ring->config->start_blk); /* block number must be wrapped now */
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  blk = ring->config->start_blk;
  nandRingUmount(ring);
  nandRingMount(ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);

  chHeapFree(pagebuf);
}

/*
 ******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************
 */

/**
 *
 */
void nandRingTest(NANDDriver *nandp) {

  nandRingObjectInit(&nandring);
  get_bad(nandp);

  nandringcfg.nandp = nandp;
  nandRingStart(&nandring, &nandringcfg);
  mount_erased(&nandring);
  //mount_trashed(&nandring);
  write_page_test(&nandring);
}







