/*
    ChibiOS/RT - Copyright (C) 2013-2014 Uladzimir Pylinsky aka barthess

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/*
 * Hardware notes.
 *
 * Use external pullup on ready/busy pin of NAND IC for a speed reason.
 *
 * Chose MCU with 140 (or more) pins package because 100 pins packages
 * has no dedicated interrupt pins for FSMC.
 *
 * If your hardware already done using 100 pin package than you have to:
 * 1) connect ready/busy pin to GPIOD6 (NWAIT in terms of STM32)
 * 2) set GPIOD6 pin as input with pullup and connect it to alternate
 * function0 (not function12)
 * 3) set up EXTI to catch raising edge on GPIOD6 and call NAND driver's
 * isr_handler() function from an EXTI callback.
 *
 * If you use MLC flash memory do NOT use ECC to detect/correct
 * errors because of its weakness. Use Rid-Solomon on BCH code instead.
 * Yes, you have to realize it in sowftware yourself.
 */

/*
 * Software notes.
 *
 * For correct calculation of timing values you need AN2784 document
 * from STMicro.
 */

#include <string.h>
#include <stdlib.h>

#include "ch.h"
#include "hal.h"

#include "bitmap.h"
#include "nand_log.h"

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */

#define USE_BAD_MAP               TRUE

#define FSMCNAND_TIME_SET         ((uint32_t) 2) //(8nS)
#define FSMCNAND_TIME_WAIT        ((uint32_t) 6) //(30nS)
#define FSMCNAND_TIME_HOLD        ((uint32_t) 1) //(5nS)
#define FSMCNAND_TIME_HIZ         ((uint32_t) 4) //(20nS)

#define NAND_BLOCKS_COUNT         8192
#define NAND_PAGE_DATA_SIZE       2048
#define NAND_PAGE_SPARE_SIZE      64
#define NAND_PAGE_SIZE            (NAND_PAGE_SPARE_SIZE + NAND_PAGE_DATA_SIZE)
#define NAND_PAGES_PER_BLOCK      64
#define NAND_ROW_WRITE_CYCLES     3
#define NAND_COL_WRITE_CYCLES     2

#define NAND_TEST_START_BLOCK     1024
#define NAND_TEST_END_BLOCK       2048

#if STM32_NAND_USE_FSMC_NAND1
  #define NAND                    NANDD1
#elif STM32_NAND_USE_FSMC_NAND2
  #define NAND                    NANDD2
#else
#error "You should enable at least one NAND interface"
#endif

#define BAD_MAP_LEN           (NAND_BLOCKS_COUNT / (sizeof(bitmap_word_t) * 8))

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
#if STM32_NAND_USE_EXT_INT
#error "External interrupt mode is not a goal of this test"
#endif

/*
 ******************************************************************************
 * GLOBAL VARIABLES
 ******************************************************************************
 */

/*
 *
 */
//static time_measurement_t tmu_erase;
//static time_measurement_t tmu_write_data;
//static time_measurement_t tmu_write_spare;
//static time_measurement_t tmu_read_data;
//static time_measurement_t tmu_read_spare;
static time_measurement_t tmu_driver_start;
//static time_measurement_t tmu_search_timestamp;

static bitmap_word_t badblock_map_array[BAD_MAP_LEN];
static bitmap_t badblock_map = {
    badblock_map_array,
    BAD_MAP_LEN
};

/*
 *
 */
static const NANDConfig nandcfg = {
    NAND_BLOCKS_COUNT,
    NAND_PAGE_DATA_SIZE,
    NAND_PAGE_SPARE_SIZE,
    NAND_PAGES_PER_BLOCK,
    NAND_ROW_WRITE_CYCLES,
    NAND_COL_WRITE_CYCLES,
    /* stm32 specific fields */
    ((FSMCNAND_TIME_HIZ << 24) | (FSMCNAND_TIME_HOLD << 16) | \
                                 (FSMCNAND_TIME_WAIT << 8) | FSMCNAND_TIME_SET),
};

static const NandRingConfig nandringcfg = {
  NAND_TEST_START_BLOCK,
  NAND_TEST_END_BLOCK,
  &NAND
};

static NandRing nandring;

static NandLog nandlog;

/*
 ******************************************************************************
 ******************************************************************************
 * LOCAL FUNCTIONS
 ******************************************************************************
 ******************************************************************************
 */
static void nand_wp_assert(void)   {palClearPad(GPIOB, GPIOB_NAND_WP);}
static void nand_wp_release(void)  {palSetPad(GPIOB, GPIOB_NAND_WP);}
static void red_led_on(void)       {palSetPad(GPIOI, GPIOI_LED_R);}
static void red_led_off(void)      {palClearPad(GPIOI, GPIOI_LED_R);}


///*
// * Benchmark for page based brute force search
// */
//static void search_timestamp(NANDDriver *nandp) {
//  uint8_t buf[18];
//  size_t p, b;

//  for (b=0; b<NAND_BLOCKS_COUNT; b++) {
//    for (p=0; p<NAND_PAGES_PER_BLOCK; p++) {
//      nandReadPageSpare(nandp, b, p, buf, sizeof(buf));
//    }
//  }
//}

/*
 ******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************
 */

/*
 * Application entry point.
 */
int main(void) {

  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  halInit();
  chSysInit();

  chTMObjectInit(&tmu_driver_start);
  chTMStartMeasurementX(&tmu_driver_start);
  nandStart(&NAND, &nandcfg, &badblock_map);
  chTMStopMeasurementX(&tmu_driver_start);


  nand_wp_release();

  nandRingObjectInit(&nandring);
  nandRingStart(&nandring, &nandringcfg);
  nandRingMount(&nandring);

  nandLogObjectInit(&nandlog);
  nandLogStart(&nandlog, &nandring);
  nandLogWrite(&nandlog, NULL, NAND_PAGE_SIZE);

  nand_wp_assert();

  /*
   * Normal main() thread activity, in this demo it does nothing.
   */
  while (true) {
    chThdSleepMilliseconds(500);
    red_led_on();
    chThdSleepMilliseconds(500);
    red_led_off();
  }
}


