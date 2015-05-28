#include <string.h>
#include <stdio.h>

#include "ch.h"
#include "hal.h"

#include "chprintf.h"

#include "cli.hpp"
#include "cli_cmd.hpp"

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
 * GLOBAL VARIABLES
 ******************************************************************************
 */

/*
 *******************************************************************************
 * EXPORTED FUNCTIONS
 *******************************************************************************
 */

/**
 *
 */
thread_t* clear_clicmd(int argc, const char * const * argv, SerialDriver *sdp){
  (void)sdp;
  (void)argc;
  (void)argv;
  cli_print("\033[2J");    // ESC seq for clear entire screen
  cli_print("\033[H");     // ESC seq for move cursor at left-top corner
  return NULL;
}

/**
 *
 */
thread_t* echo_clicmd(int argc, const char * const * argv, SerialDriver *sdp){
  (void)sdp;

  int i = 0;
  while (i < argc)
    cli_print(argv[i++]);

  cli_print(ENDL);
  return NULL;
}

/**
 *
 */
thread_t* reboot_clicmd(int argc, const char * const * argv, SerialDriver *sdp){
  (void)sdp;
  (void)argv;
  (void)argc;
  cli_print("System going to reboot now...\r\n");
  chThdSleepMilliseconds(100);
  NVIC_SystemReset();
  return NULL;
}

/**
 *
 */
thread_t* sleep_clicmd(int argc, const char * const * argv, SerialDriver *sdp){
  (void)sdp;
  (void)argv;
  (void)argc;

  cli_print("System sleeping.\r\n");
  cli_print("Press any key to wake it up.\r\n");
  chThdSleepMilliseconds(100);

  chSysLock();
  PWR->CR |= (PWR_CR_PDDS | PWR_CR_LPDS | PWR_CR_CSBF | PWR_CR_CWUF);
  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
  __WFI();
  return NULL;
}

/**
 *
 */
thread_t* selftest_clicmd(int argc, const char * const * argv, SerialDriver *sdp){
  (void)sdp;
  (void)argv;
  (void)argc;

  cli_print("GPS - OK\r\nModem - OK\r\nEEPROM - OK\r\nStorage - OK\r\nServos - OK\r\n");
  return NULL;
}

/**
 *
 */
thread_t* uname_clicmd(int argc, const char * const * argv, SerialDriver *sdp){
  (void)sdp;
  (void)argc;
  (void)argv;

  BaseSequentialStream* chp = (BaseSequentialStream*)sdp;

  chprintf(chp, "Kernel:       %s\r\n", CH_KERNEL_VERSION);
#ifdef PORT_COMPILER_NAME
  chprintf(chp, "Compiler:     %s\r\n", PORT_COMPILER_NAME);
#endif
  chprintf(chp, "Architecture: %s\r\n", PORT_ARCHITECTURE_NAME);
#ifdef PORT_CORE_VARIANT_NAME
  chprintf(chp, "Core Variant: %s\r\n", PORT_CORE_VARIANT_NAME);
#endif
#ifdef PORT_INFO
  chprintf(chp, "Port Info:    %s\r\n", PORT_INFO);
#endif
#ifdef PLATFORM_NAME
  chprintf(chp, "Platform:     %s\r\n", PLATFORM_NAME);
#endif
#ifdef BOARD_NAME
  chprintf(chp, "Board:        %s\r\n", BOARD_NAME);
#endif
#ifdef __DATE__
#ifdef __TIME__
  chprintf(chp, "Build time:   %s%s%s\r\n", __DATE__, " - ", __TIME__);
#endif
#endif
  return NULL;
}





