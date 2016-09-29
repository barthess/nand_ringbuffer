#include <string.h>
#include <stdlib.h>

#include "ch.h"
#include "hal.h"

#include "nand_log.h"
#include "nand_log_test.h"
#include "linetest_proto.h"

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */

#define NAND_TEST_START_BLOCK     (4096)
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

static NandLog nandlog;

static LinetestParser line_parser;

static uint32_t WrittenBytesTotal = 0;

/*
 ******************************************************************************
 ******************************************************************************
 * LOCAL FUNCTIONS
 ******************************************************************************
 ******************************************************************************
 */

/**
 * @brief write_block_test
 * @param nandlog
 */
void write_block_test(NandLog *nandlog) {
  size_t N = 100;

  const uint8_t *data = LinetestParserFill(&line_parser, N);
  size_t wr = nandLogWrite(nandlog, data, N);
  osalDbgCheck (N == wr);
  WrittenBytesTotal += N;
  osalThreadSleepMilliseconds(20);
}

/*
 ******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************
 */

/**
 *
 */
void nandLogTest(NANDDriver *nandp, const NANDConfig *config, bitmap_t *bb_map) {

  srand(chSysGetRealtimeCounterX());

  nandRingObjectInit(&nandring);
  nandLogObjectInit(&nandlog);
  LinetestParserObjectInit(&line_parser);

  nandStart(nandp, config, bb_map);
  nandringcfg.nandp = nandp;
  uint8_t *ring_working_area = chHeapAlloc(NULL, nandRingWASize(nandp));

  nandLogStart(&nandlog, &nandring, &nandringcfg, ring_working_area);



  WrittenBytesTotal = 0;
  while(WrittenBytesTotal < (512 * 1000)) {
    write_block_test(&nandlog);
  }



  nandLogStop(&nandlog);
  nandRingUmount(&nandring);
  nandRingStop(&nandring);
  chHeapFree(ring_working_area);
}

