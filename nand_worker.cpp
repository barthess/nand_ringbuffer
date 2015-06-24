#include "ch.hpp"
#include "hal.h"
#include "string.h"

#include "nand_worker.hpp"

using namespace chibios_rt;

/*
 * Асинхронная неблокирующая надстройка.
 */

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
THD_FUNCTION(NandWorker::WorkerThread, arg) {
  chRegSetThreadName("NandWorker");
  NandWorker *self = static_cast<NandWorker *>(arg);
  uint8_t *data = nullptr;

  while (!chThdShouldTerminateX()) {
    if (MSG_OK == self->mailbox.fetch(&data, MS2ST(200))) {
      self->ring->writePage(data);
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
 * @pre  NAND must be configured and started externally
 */
NandWorker::NandWorker(void) : ready(false), ring(nullptr), worker(nullptr)
{
  ;
}

/**
 * @brief NandWorker::start
 * @param ring
 */
void NandWorker::start(NandRing *ring) {

  if (ready) {
    return;
  }
  else {
    this->ring = ring;
    this->worker = chThdCreateStatic(NandWorkerThreadWA, sizeof(NandWorkerThreadWA),
                                     NORMALPRIO, WorkerThread, this);
    osalDbgAssert(nullptr != this->worker, "Can not allocate memroy");
    this->ready = true;
  }
}

/**
 * @brief NandWorker::start
 * @param ring
 */
void NandWorker::stop(void) {

  if (!ready) {
    return;
  }
  else {
    this->ready = false;
    chThdTerminate(this->worker);
    chThdWait(this->worker);
    this->worker = nullptr;
    this->ring = nullptr;
  }
}

/**
 *
 */
size_t NandWorker::append(const uint8_t *data, size_t len) {
  size_t written = 0;

  osalDbgCheck(ready);

  uint8_t *ptr = multibuf.append(data, len, &written);

  if (nullptr != ptr) {
    // There is not check for post status because mailbox has exactly the
    // same slots count as multibuffer has.
    mailbox.post(ptr, TIME_IMMEDIATE);
  }

  return written;
}
