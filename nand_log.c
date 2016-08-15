#include <string.h>

#include "ch.h"
#include "hal.h"

#include "nand_log.h"

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */
#define FETCH_TIMEOUT     (MS2ST(200))
#define NAND_PAGE_SIZE    2048 // TODO: delete hardcode

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

static msg_t nand_mailbox_buffer[NAND_BUFFER_COUNT];
MAILBOX_DECL(nand_mailbox, nand_mailbox_buffer, NAND_BUFFER_COUNT);

MEMORYPOOL_DECL(nand_mempool, NAND_PAGE_SIZE, NULL) ;
static uint8_t nand_mempool_buffer[NAND_PAGE_SIZE * NAND_BUFFER_COUNT];

/*
 ******************************************************************************
 ******************************************************************************
 * LOCAL FUNCTIONS
 ******************************************************************************
 ******************************************************************************
 */

/**
 * @brief post_full_buffer
 * @param log
 */
static void post_full_buffer(NandLog *log) {

  msg_t msg = (msg_t)(log->storage.cur - NAND_PAGE_SIZE);
  chMBPost(log->mb, msg, TIME_IMMEDIATE);
}

/**
 * @brief zero_tail
 * @param log
 */
void zero_tail(NandLog *log) {
  NandBuffer *storage = &log->storage;

  if ((NULL != storage->cur) && (storage->free > 0)) {
    memset(storage->cur, 0, storage->free);
    storage->cur += storage->free;
  }
}

/**
 *
 */
static THD_WORKING_AREA(NandWorkerThreadWA, 320);
static THD_FUNCTION(NandWorker, arg) {
  chRegSetThreadName("NandLog");
  NandLog *self = arg;
  uint8_t *data = NULL;

  while (! chThdShouldTerminateX()) {
    if (MSG_OK == chMBFetch(self->mb, (msg_t *)(&data), FETCH_TIMEOUT)) {
      nandRingWritePage(self->ring, data);
      chPoolFree(self->storage.mempool, data);
    }
  }

  /* flush data and free all allocated buffers if any */
  osalSysLock();
  size_t used = chMBGetUsedCountI(self->mb);
  osalSysUnlock();
  while (used--) {
    chMBFetch(self->mb, (msg_t *)(&data), TIME_IMMEDIATE);
    nandRingWritePage(self->ring, data);
    chPoolFree(self->storage.mempool, data);
  }

  chThdExit(MSG_OK);
}

/*
 ******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************
 */

/**
 * @brief nandLogObjectInit
 * @param log
 */
void nandLogObjectInit(NandLog *log) {
  log->mb = NULL;
  log->worker = NULL;
  log->ring = NULL;
  log->state = NAND_LOG_STOP;

  log->storage.free = 0;
  log->storage.cur = NULL;
  log->storage.mempool = NULL;
}

/**
 * @brief nandLogStart
 * @param log
 * @param ring
 */
void nandLogStart(NandLog *log, NandRing2 *ring) {

  osalDbgCheck((NULL != log) && (NULL != ring));
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);

  log->mb = &nand_mailbox;
  log->ring = ring;

  chPoolLoadArray(&nand_mempool, nand_mempool_buffer, NAND_BUFFER_COUNT);
  log->storage.free = NAND_PAGE_SIZE;
  log->storage.mempool = &nand_mempool;
  log->storage.cur = chPoolAlloc(&nand_mempool);

  log->worker = chThdCreateStatic(NandWorkerThreadWA, sizeof(NandWorkerThreadWA),
                                  NORMALPRIO, NandWorker, log);
  osalDbgAssert(NULL != log->worker, "Can not allocate memroy");
  log->state = NAND_LOG_READY;
}

/**
 * @brief nandLogWrite
 * @param log
 * @param data
 * @param len
 * @note      There is no checks of mailbox post status because it has the
 *            same size as memory pool.
 * @return    size of actually written data
 */
size_t nandLogWrite(NandLog *log, const uint8_t *data, size_t len) {

  osalDbgCheck((NULL != log) && (NULL != data) && (0 != len));
  osalDbgCheck(NAND_LOG_READY == log->state);
  size_t written = 0;
  NandBuffer *storage = &log->storage;

  /* first look for available buffers and try to allocate new one
     if all of them was exhausted during previouse operation */
  if (NULL == storage->cur) {
    storage->cur = chPoolAlloc(storage->mempool);
    if (NULL == storage->cur) {
      return 0;
    }
  }

  /* write main data block */
  while (len >= storage->free) {
    memcpy(storage->cur, data, storage->free);

    len -= storage->free;
    written += storage->free;
    storage->cur += len;
    post_full_buffer(log);

    storage->free = NAND_PAGE_SIZE;
    storage->cur  = chPoolAlloc(storage->mempool);
    if (NULL == storage->cur) {
      /* memory pool exhausted */
      return written;
    }
  }

  /* write data tail if any */
  memcpy(storage->cur, data, len);
  storage->free -= len;
  storage->cur += len;
  written += len;

  return written;
}

/**
 * @brief nandLogStop
 * @param log
 */
void nandLogStop(NandLog *log) {

  if (NAND_LOG_READY == log->state) {
    log->state = NAND_LOG_STOP;

    zero_tail(log);
    post_full_buffer(log);

    chThdTerminate(log->worker);
    chThdWait(log->worker);
    log->worker = NULL;
    log->ring = NULL;
  }
}

