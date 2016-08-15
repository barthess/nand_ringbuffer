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
//#define NAND_PAGE_SIZE    2048 // TODO: delete hardcode

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

//static msg_t nand_mailbox_buffer[NAND_BUFFER_COUNT];
//MAILBOX_DECL(nand_mailbox, nand_mailbox_buffer, NAND_BUFFER_COUNT);

//MEMORYPOOL_DECL(nand_mempool, NAND_PAGE_SIZE, NULL) ;
//static uint8_t nand_mempool_buffer[NAND_PAGE_SIZE * NAND_BUFFER_COUNT];

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

  const size_t pagesize = log->ring->config->nandp->config->page_data_size;
  msg_t msg = (msg_t)(log->btip - pagesize);

  chMBPost(&log->mb, msg, TIME_IMMEDIATE);
}

/**
 * @brief zero_tail
 * @param log
 */
void zero_tail(NandLog *log) {

  if ((NULL != log->btip) && (log->bfree > 0)) {
    memset(log->btip, 0, log->bfree);
    log->btip += log->bfree;
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
    if (MSG_OK == chMBFetch(&self->mb, (msg_t *)(&data), FETCH_TIMEOUT)) {
      nandRingWritePage(self->ring, data);
      chPoolFree(&self->mempool, data);
    }
  }

  /* flush data and free all allocated buffers if any */
  osalSysLock();
  size_t used = chMBGetUsedCountI(&self->mb);
  osalSysUnlock();
  while (used--) {
    chMBFetch(&self->mb, (msg_t *)(&data), TIME_IMMEDIATE);
    nandRingWritePage(self->ring, data);
    chPoolFree(&self->mempool, data);
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

  chMBObjectInit(&log->mb, log->mailbox_buf, NAND_BUFFER_COUNT);

  log->worker = NULL;
  log->ring = NULL;
  log->state = NAND_LOG_STOP;

  log->bfree = 0;
  log->btip = NULL;
  log->mempool_buf = NULL;
}

/**
 * @brief nandLogStart
 * @param log
 * @param ring
 */
void nandLogStart(NandLog *log, NandRing *ring) {

  osalDbgCheck((NULL != log) && (NULL != ring));
  osalDbgCheck(NAND_RING_MOUNTED == ring->state);
  const size_t pagesize = ring->config->nandp->config->page_data_size;

  /* pool pointer does not nulls during stop procedure */
  if (NULL == log->mempool_buf) {
    log->mempool_buf = chCoreAlloc(pagesize * NAND_BUFFER_COUNT);
    chPoolObjectInit(&log->mempool, pagesize, NULL);
    chPoolLoadArray(&log->mempool, log->mempool_buf, NAND_BUFFER_COUNT);
  }

  log->ring = ring;
  log->bfree = pagesize;
  log->btip = chPoolAlloc(&log->mempool);

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
 * @return    Size of actually written data.
 */
size_t nandLogWrite(NandLog *log, const uint8_t *data, size_t len) {

  osalDbgCheck((NULL != log) && (NULL != data) && (0 != len));
  osalDbgCheck(NAND_LOG_READY == log->state);
  size_t written = 0;
  const size_t pagesize = log->ring->config->nandp->config->page_data_size;

  /* first look for available buffers and try to allocate new one
     if all of them was exhausted during previouse operation */
  if (NULL == log->btip) {
    log->btip = chPoolAlloc(&log->mempool);
    if (NULL == log->btip) {
      return 0;
    }
  }

  /* write main data block */
  while (len >= log->bfree) {
    memcpy(log->btip, data, log->bfree);

    len -= log->bfree;
    written += log->bfree;
    log->btip += len;
    post_full_buffer(log);

    log->bfree = pagesize;
    log->btip  = chPoolAlloc(&log->mempool);
    if (NULL == log->btip) {
      /* memory pool exhausted */
      return written;
    }
  }

  /* write data tail if any */
  memcpy(log->btip, data, len);
  log->bfree -= len;
  log->btip += len;
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

