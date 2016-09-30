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

#define NAND_TEST_START_BLOCK     (2300)
#define NAND_TEST_LEN             100
#define NAND_TEST_LAST_BLOCK      (NAND_TEST_START_BLOCK + NAND_TEST_LEN - 1)

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

static uint16_t badblocks_table[64];

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
void fill_bad_table(NANDDriver *nandp) {

  size_t tip = 0;
  memset(badblocks_table, 0, sizeof(badblocks_table));
  for (size_t b=0; b<nandp->config->blocks; b++) {
    if (nandIsBad(nandp, b)) {
      badblocks_table[tip] = b;
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
static bool is_sequence_good(NandRing *ring) {

  return ring->config->len == nandRingTotalGood(ring);
}

/**
 * @brief mount_erased
 * @param ring
 */
void mount_erased(NandRing *ring) {

  const size_t start = ring->config->start_blk;
  const size_t len   = ring->config->len;
  NANDDriver *nandp  = ring->config->nandp;

  __nandEraseRangeForce(nandp, start, len);
  const NANDConfig *cfg = nandp->config;
  bitmap_t *bb_map = nandp->bb_map;
  nandStop(nandp);
  nandStart(nandp, cfg, bb_map);
  osalDbgCheck(is_sequence_good(ring));

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
void mount_trashed(NandRing *ring) {

  const size_t start = ring->config->start_blk;
  const size_t len   = ring->config->len;
  NANDDriver *nandp  = ring->config->nandp;
  osalDbgCheck(is_sequence_good(ring));

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
void write_page_test(NandRing *ring) {

  NANDDriver *nandp  = ring->config->nandp;
  const size_t pds = nandp->config->page_data_size;
  uint8_t *pagebuf = chHeapAlloc(NULL, pds);
  uint64_t id = 1;
  uint32_t blk = ring->config->start_blk;
  uint32_t len = ring->config->len;

  /* this test does not check bad block handling */
  osalDbgCheck(is_sequence_good(ring));

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
  nandRingUmount(ring);
  nandRingMount(ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == ring->config->start_blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);

  /*
   * check full storage triple write at single session
   */
  id = 1;
  nandRingUmount(ring);
  nandEraseRange(nandp, ring->config->start_blk, len);
  nandRingMount(ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == ring->config->start_blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  for (size_t b=0; b<3*len; b++) {
    for (size_t i=0; i<nandp->config->pages_per_block; i++){
      nandRingWritePage(ring, pagebuf);
      id++;
    }
  }
  osalDbgCheck(ring->cur_blk  == ring->config->start_blk); /* block number must be wrapped now */
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  nandRingUmount(ring);
  nandRingMount(ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == ring->config->start_blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  nandRingUmount(ring);

  chHeapFree(pagebuf);
}

/**
 * @brief write_page_test
 * @param ring
 */
void mount_erased_with_bad(NandRing *ring) {

  NANDDriver *nandp = ring->config->nandp;
  const size_t pds = nandp->config->page_data_size;
  uint8_t *pagebuf = chHeapAlloc(NULL, pds);
  uint64_t id = 1;
  uint32_t blk = ring->config->start_blk;
  uint32_t len = ring->config->len;

  /* All blocks must be good before test. Some of them will
     be marked bad during test, and must be erased at the end of test. */
  osalDbgCheck(is_sequence_good(ring));

  /*
   * first block is bad
   */
  __nandEraseRangeForce(nandp, blk, len);
  nandMarkBad(nandp, blk);
  nandRingMount(ring);
  osalDbgCheck(1 == len - nandRingTotalGood(ring));
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == blk+1);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  nandRingWritePage(ring, pagebuf);
  id++;
  osalDbgCheck(ring->cur_blk  == blk+1);
  osalDbgCheck(ring->cur_page == 1);
  osalDbgCheck(ring->cur_id   == id);
  nandRingUmount(ring);
  __nandEraseRangeForce(nandp, blk, 2);

  /*
   * first 3 blocks and last block is bad
   */
  id = 1;
  blk = ring->config->start_blk;
  len = ring->config->len;
  nandMarkBad(nandp, blk);
  nandMarkBad(nandp, blk+1);
  nandMarkBad(nandp, blk+2);
  nandMarkBad(nandp, blk+len-1);
  nandRingMount(ring);
  blk+=3;
  osalDbgCheck(4 == len - nandRingTotalGood(ring));
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  /* now write data to the end of ring */
  for (; blk < (ring->config->start_blk + len - 1); blk++) {
    for (size_t i=0; i<nandp->config->pages_per_block; i++) {
      nandRingWritePage(ring, pagebuf);
      id++;
    }
  }
  osalDbgCheck(4 == len - nandRingTotalGood(ring));
  osalDbgCheck(ring->cur_blk  == ring->config->start_blk + 3);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  nandRingUmount(ring);
  nandRingMount(ring);
  osalDbgCheck(4 == len - nandRingTotalGood(ring));
  osalDbgCheck(ring->cur_blk  == ring->config->start_blk + 3);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  nandRingUmount(ring);
  __nandEraseRangeForce(nandp, ring->config->start_blk, ring->config->len);

  /*
   * pattern ---------x-x-xx-x---------------
   * in the middle of ring
   * NOTE: blocks previously marked bad did not unmarked during erase because
   * bad block map unchanged.
   */
  id = 1;
  blk = ring->config->start_blk + 3;
  len = ring->config->len;
  nandMarkBad(nandp, blk + 10);
  nandMarkBad(nandp, blk + 12);
  nandMarkBad(nandp, blk + 14);
  nandMarkBad(nandp, blk + 15);
  nandMarkBad(nandp, blk + 17);
  nandRingMount(ring);
  osalDbgCheck(4+5 == len - nandRingTotalGood(ring));
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  /* now write data to the end of ring */

  for (; blk < (ring->config->start_blk + len - (1+5)); blk++) {
    for (size_t i=0; i<nandp->config->pages_per_block; i++) {
      nandRingWritePage(ring, pagebuf);
      id++;
    }
  }
  osalDbgCheck(4+5 == len - nandRingTotalGood(ring));
  osalDbgCheck(ring->cur_blk  == ring->config->start_blk + 3);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  nandRingUmount(ring);
  nandRingMount(ring);
  osalDbgCheck(4+5 == len - nandRingTotalGood(ring));
  osalDbgCheck(ring->cur_blk  == ring->config->start_blk + 3);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);

  /*
   * make clean
   */
  nandRingUmount(ring);
  __nandEraseRangeForce(nandp, ring->config->start_blk, ring->config->len);
  chHeapFree(pagebuf);
}

/**
 *
 */
void mount_fail_test(NandRing *ring) {
  NANDDriver *nandp = ring->config->nandp;
  uint32_t blk = ring->config->start_blk;
  uint32_t len = ring->config->len;

  /* All blocks must be good before test. Some of them will
     be marked bad during test, and must be erased at the end of test. */
  osalDbgCheck(is_sequence_good(ring));

  /*
   * mount must failed when more than half of blocks are bad
   */
  for (size_t b=0; b<len/2+3; b++) {
    nandMarkBad(nandp, blk+b);
  }
  osalDbgCheck(OSAL_FAILED == nandRingMount(ring));
  osalDbgCheck(NAND_RING_IDLE == ring->state);
  __nandEraseRangeForce(nandp, blk, len);
}

/**
 *
 */
void error_handling(NandRing *ring) {

  NANDDriver *nandp = ring->config->nandp;
  const size_t pds = nandp->config->page_data_size;
  uint8_t *pagebuf = chHeapAlloc(NULL, pds);
  uint64_t id = 1;
  uint32_t blk = ring->config->start_blk;
  uint32_t len = ring->config->len;

  /* All blocks must be good before test. Some of them will
     be marked bad during test, and must be erased at the end of test. */
  osalDbgCheck(is_sequence_good(ring));

  /*
   * switch error injection ON and write some full rings of data
   */
  osalDbgCheck(OSAL_SUCCESS == nandRingMount(ring));
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_blk  == blk);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  __nandSetErrorChance(4096);
  for (size_t b=0; b<7*len; b++) {
    for (size_t i=0; i<nandp->config->pages_per_block; i++) {
      bool status = nandRingWritePage(ring, pagebuf);
      osalDbgCheck(OSAL_SUCCESS == status);
      id++;
    }
  }
  nandRingUmount(ring);
  __nandSetErrorChance(0);
  osalDbgCheck(OSAL_SUCCESS == nandRingMount(ring));

  /*
   * increse error rate and write data until no space left on ring
   */
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  osalDbgCheck(ring->cur_page == 0);
  osalDbgCheck(ring->cur_id   == id);
  __nandSetErrorChance(512);
  bool status = OSAL_SUCCESS;
  while (OSAL_SUCCESS == status) {
    status = nandRingWritePage(ring, pagebuf);
    id++;
  }
  nandRingUmount(ring);
  osalDbgCheck(OSAL_FAILED == nandRingMount(ring));

  /*
   * make clean
   */
  __nandEraseRangeForce(nandp, ring->config->start_blk, ring->config->len);
  chHeapFree(pagebuf);
}

/**
 * @brief iterator_empty_test
 * @param ring
 */
void iterator_empty(NandRing *ring) {

  const size_t start = ring->config->start_blk;
  const size_t len   = ring->config->len;
  NANDDriver *nandp  = ring->config->nandp;
  NandRingIterator it;
  NandRingSession session;

  nandEraseRange(nandp, start, len);
  osalDbgCheck(is_sequence_good(ring));
  nandRingMount(ring);

  NandRingIteratorBind(&it, ring);
  osalDbgCheck(NAND_RING_ITERATOR_BOUNDED == it.ring->state);
  osalDbgCheck(NandRingIteratorFinished(&it));
  osalDbgCheck(OSAL_FAILED == NandRingIteratorNext(&it, &session));
  NandRingIteratorRelease(&it);
  osalDbgCheck(NULL == it.ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);

  nandRingUmount(ring);
}

/**
 *
 */
void iterator_single_session(NandRing *ring) {
  const size_t start = ring->config->start_blk;
  const size_t len   = ring->config->len;
  NANDDriver *nandp  = ring->config->nandp;
  const size_t ppb = nandp->config->pages_per_block;
  const size_t pds = nandp->config->page_data_size;
  uint8_t *pagebuf = chHeapAlloc(NULL, pds);
  NandRingIterator it;
  NandRingSession session;
  bool status;

  osalDbgCheck(is_sequence_good(ring));
  nandEraseRange(nandp, start, len);
  nandRingMount(ring);

  /* single page written */
  status = nandRingWritePage(ring, pagebuf);
  osalDbgCheck(OSAL_SUCCESS == status);
  NandRingIteratorBind(&it, ring);
  osalDbgCheck(NAND_RING_ITERATOR_BOUNDED == it.ring->state);
  osalDbgCheck(! NandRingIteratorFinished(&it));
  osalDbgCheck(it.last_blk == NAND_TEST_START_BLOCK);
  osalDbgCheck(OSAL_SUCCESS == NandRingIteratorNext(&it, &session));
  osalDbgCheck(NandRingIteratorFinished(&it));
  osalDbgCheck(session.first_blk == NAND_TEST_START_BLOCK);
  osalDbgCheck(session.last_blk == NAND_TEST_START_BLOCK);
  osalDbgCheck(session.last_page == 0);
  NandRingIteratorRelease(&it);
  osalDbgCheck(NULL == it.ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);

  /* single block written */
  for (size_t i=1; i<ppb; i++) {
    status = nandRingWritePage(ring, pagebuf);
    osalDbgCheck(OSAL_SUCCESS == status);
  }
  NandRingIteratorBind(&it, ring);
  osalDbgCheck(NAND_RING_ITERATOR_BOUNDED == it.ring->state);
  osalDbgCheck(! NandRingIteratorFinished(&it));
  osalDbgCheck(it.last_blk == NAND_TEST_START_BLOCK);
  osalDbgCheck(OSAL_SUCCESS == NandRingIteratorNext(&it, &session));
  osalDbgCheck(NandRingIteratorFinished(&it));
  osalDbgCheck(session.first_blk == NAND_TEST_START_BLOCK);
  osalDbgCheck(session.last_blk == NAND_TEST_START_BLOCK);
  osalDbgCheck(session.last_page == ppb - 1);
  NandRingIteratorRelease(&it);

  /* block + page written */
  status = nandRingWritePage(ring, pagebuf);
  osalDbgCheck(OSAL_SUCCESS == status);
  NandRingIteratorBind(&it, ring);
  osalDbgCheck(NAND_RING_ITERATOR_BOUNDED == it.ring->state);
  osalDbgCheck(! NandRingIteratorFinished(&it));
  osalDbgCheck(it.last_blk == NAND_TEST_START_BLOCK + 1);
  osalDbgCheck(OSAL_SUCCESS == NandRingIteratorNext(&it, &session));
  osalDbgCheck(NandRingIteratorFinished(&it));
  osalDbgCheck(session.first_blk == NAND_TEST_START_BLOCK);
  osalDbgCheck(session.last_blk == NAND_TEST_START_BLOCK + 1);
  osalDbgCheck(session.last_page == 0);
  NandRingIteratorRelease(&it);
  osalDbgCheck(NULL == it.ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);

  /* ring written */
  for (size_t i=1; i<ppb; i++) {
    status = nandRingWritePage(ring, pagebuf);
    osalDbgCheck(OSAL_SUCCESS == status);
  }
  for (size_t b=2; b<NAND_TEST_LEN; b++) {
    for (size_t i=0; i<ppb; i++) {
      status = nandRingWritePage(ring, pagebuf);
      osalDbgCheck(OSAL_SUCCESS == status);
    }
  }
  NandRingIteratorBind(&it, ring);
  osalDbgCheck(NAND_RING_ITERATOR_BOUNDED == it.ring->state);
  osalDbgCheck(! NandRingIteratorFinished(&it));
  osalDbgCheck(it.last_blk == NAND_TEST_LAST_BLOCK);
  osalDbgCheck(OSAL_SUCCESS == NandRingIteratorNext(&it, &session));
  osalDbgCheck(NandRingIteratorFinished(&it));
  osalDbgCheck(session.first_blk == NAND_TEST_START_BLOCK + 1);
  osalDbgCheck(session.last_blk == NAND_TEST_LAST_BLOCK);
  osalDbgCheck(session.last_page == ppb - 1);
  NandRingIteratorRelease(&it);
  osalDbgCheck(NULL == it.ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);

  /* Uroboros tests */
  status = nandRingWritePage(ring, pagebuf);
  osalDbgCheck(OSAL_SUCCESS == status);
  NandRingIteratorBind(&it, ring);
  osalDbgCheck(NAND_RING_ITERATOR_BOUNDED == it.ring->state);
  osalDbgCheck(! NandRingIteratorFinished(&it));
  osalDbgCheck(it.last_blk == NAND_TEST_START_BLOCK);
  osalDbgCheck(OSAL_SUCCESS == NandRingIteratorNext(&it, &session));
  osalDbgCheck(NandRingIteratorFinished(&it));
  osalDbgCheck(session.first_blk == NAND_TEST_START_BLOCK + 1);
  osalDbgCheck(session.last_blk == NAND_TEST_START_BLOCK);
  osalDbgCheck(session.last_page == 0);
  NandRingIteratorRelease(&it);
  osalDbgCheck(NULL == it.ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);

  for (size_t i=1; i<ppb; i++) {
    status = nandRingWritePage(ring, pagebuf);
    osalDbgCheck(OSAL_SUCCESS == status);
  }
  for (size_t b=1; b<10; b++) {
    for (size_t i=0; i<ppb; i++) {
      status = nandRingWritePage(ring, pagebuf);
      osalDbgCheck(OSAL_SUCCESS == status);
    }
  }
  NandRingIteratorBind(&it, ring);
  osalDbgCheck(NAND_RING_ITERATOR_BOUNDED == it.ring->state);
  osalDbgCheck(! NandRingIteratorFinished(&it));
  osalDbgCheck(it.last_blk == NAND_TEST_START_BLOCK + 9);
  osalDbgCheck(OSAL_SUCCESS == NandRingIteratorNext(&it, &session));
  osalDbgCheck(NandRingIteratorFinished(&it));
  osalDbgCheck(session.first_blk == NAND_TEST_START_BLOCK + 9 + 2);
  osalDbgCheck(session.last_blk == NAND_TEST_START_BLOCK + 9);
  osalDbgCheck(session.last_page == ppb - 1);
  NandRingIteratorRelease(&it);
  osalDbgCheck(NULL == it.ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);

  nandRingUmount(ring);
  chHeapFree(pagebuf);
}

/**
 * @brief iterator_multisession
 * @param ring
 */
void iterator_multisession(NandRing *ring) {
  const size_t start = ring->config->start_blk;
  const size_t len   = ring->config->len;
  NANDDriver *nandp  = ring->config->nandp;
  const size_t ppb = nandp->config->pages_per_block;
  const size_t pds = nandp->config->page_data_size;
  uint8_t *pagebuf = chHeapAlloc(NULL, pds);
  NandRingIterator it;
  NandRingSession session;
  bool status;
  size_t L1 = 10;
  size_t L2 = 20;

  osalDbgCheck(is_sequence_good(ring));
  nandEraseRange(nandp, start, len);

  /* 2 sessions not full ring */
  nandRingMount(ring);
  for (size_t b=0; b<L1; b++) {
    for (size_t i=0; i<ppb; i++) {
      status = nandRingWritePage(ring, pagebuf);
      osalDbgCheck(OSAL_SUCCESS == status);
    }
  }
  nandRingUmount(ring);

  nandRingMount(ring);
  for (size_t b=0; b<L2; b++) {
    for (size_t i=0; i<ppb; i++) {
      status = nandRingWritePage(ring, pagebuf);
      osalDbgCheck(OSAL_SUCCESS == status);
    }
  }
  nandRingUmount(ring);

  NandRingIteratorBind(&it, ring);
  osalDbgCheck(NAND_RING_ITERATOR_BOUNDED == it.ring->state);

  osalDbgCheck(it.last_blk == NAND_TEST_START_BLOCK);
  osalDbgCheck(OSAL_SUCCESS == NandRingIteratorNext(&it, &session));
  osalDbgCheck(NandRingIteratorFinished(&it));
  osalDbgCheck(session.first_blk == NAND_TEST_START_BLOCK);
  osalDbgCheck(session.last_blk == NAND_TEST_START_BLOCK);
  osalDbgCheck(session.last_page == 0);


  osalDbgCheck(OSAL_FAILED == NandRingIteratorNext(&it, &session));
  NandRingIteratorRelease(&it);
  osalDbgCheck(NULL == it.ring);
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
}


/*
 ******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************
 */

/**
 *
 */
void nandRingIteratorTest(NANDDriver *nandp, const NANDConfig *config, bitmap_t *bb_map) {

  nandStart(nandp, config, bb_map);
  nandRingObjectInit(&nandring);

  nandringcfg.nandp = nandp;
  uint8_t *ring_working_area = chHeapAlloc(NULL, nandRingWASize(nandp));
  nandRingStart(&nandring, &nandringcfg, ring_working_area);

  srand(chSysGetRealtimeCounterX());

  iterator_empty(&nandring);
  iterator_single_session(&nandring);
  iterator_multisession(&nandring);

  nandRingStop(&nandring);
  chHeapFree(ring_working_area);
  nandStop(nandp);
}


/**
 *
 */
void nandRingTest(NANDDriver *nandp, const NANDConfig *config, bitmap_t *bb_map) {

  osalSysHalt("You need to add back_link testing in test suite");

  nandStart(nandp, config, bb_map);
  nandRingObjectInit(&nandring);
  fill_bad_table(nandp);

  nandringcfg.nandp = nandp;
  uint8_t *ring_working_area = chHeapAlloc(NULL, nandRingWASize(nandp));
  nandRingStart(&nandring, &nandringcfg, ring_working_area);

  srand(chSysGetRealtimeCounterX());

  mount_erased(&nandring);
  mount_trashed(&nandring);
  write_page_test(&nandring);
  mount_erased_with_bad(&nandring);

  nandStop(nandp);
  nandStart(nandp, config, bb_map);
  mount_fail_test(&nandring);

  srand(chSysGetRealtimeCounterX());
  nandStop(nandp);
  nandStart(nandp, config, bb_map);
  error_handling(&nandring);

  nandRingStop(&nandring);
  chHeapFree(ring_working_area);
  nandStop(nandp);
}
