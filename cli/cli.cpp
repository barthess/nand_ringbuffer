#include <string.h>
#include <stdlib.h>

#include "microrl.h"

#include "ch.h"
#include "hal.h"

#include "cli.hpp"
#include "cli_cmd.hpp"

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */
#define _NUM_OF_CMD (sizeof(cliutils)/sizeof(ShellCmd_t))

/*
 ******************************************************************************
 * EXTERNS
 ******************************************************************************
 */

/*
 *******************************************************************************
 * PROTOTYPES
 *******************************************************************************
 */
static thread_t* help_clicmd(int argc, const char * const * argv, SerialDriver *sdp);

/*
 ******************************************************************************
 * GLOBAL VARIABLES
 ******************************************************************************
 */

static const ShellCmd_t cliutils[] = {
    {"clear",     &clear_clicmd,      "clear screen"},
    {"echo",      &echo_clicmd,       "echo it's input to terminal"},
    {"help",      &help_clicmd,       "this message"},
    {"info",      &uname_clicmd,      "system information"},
    {"reboot",    &reboot_clicmd,     "reboot system. Use with caution!"},
    {"selftest",  &selftest_clicmd,   "exectute selftests (stub)"},
    {"sleep",     &sleep_clicmd,      "put autopilot board in sleep state (do not use it)"},
    {"uname",     &uname_clicmd,      "'info' alias"},
    {NULL,        NULL,               NULL}/* end marker */
};

// array for comletion
static char *compl_world[_NUM_OF_CMD + 1];

/* thread pointer to currently executing command */
static thread_t *current_cmd_tp = NULL;

/* pointer to shell thread */
static thread_t *shell_tp = NULL;

/* serial interface for shell */
static SerialDriver *ShellSDp;

/*
 *******************************************************************************
 * LOCAL FUNCTIONS
 *******************************************************************************
 */
/**
 * Print routine for microrl.
 */
static void microrl_print(void* user_halde, const char *str){
  (void)user_halde;
  cli_print(str);
}

/**
 * Search value (pointer to function) by key (name string)
 */
static int32_t cmd_search(const char* key, const ShellCmd_t *cmdarray){
  uint32_t i = 0;

  while (cmdarray[i].name != NULL){
    if (strcmp(key, cmdarray[i].name) == 0)
      return i;
    i++;
  }
  return -1;
}

//*****************************************************************************
// execute callback for microrl library
// do what you want here, but don't write to argv!!! read only!!
static unsigned int execute (void* user_handle, int argc, const char * const * argv){
  (void)user_handle;

  int i = 0;

  /* search first token */
  i = cmd_search(argv[0], cliutils);
  if (i == -1){
    cli_print ("command: '");
    cli_print ((char*)argv[0]);
    cli_print ("' Not found.\n\r");
  }
  else{
    if (argc > 1)
      current_cmd_tp = cliutils[i].func(argc - 1, &argv[1], ShellSDp);
    else
      current_cmd_tp = cliutils[i].func(0, NULL, ShellSDp);
  }
  return 0;
}

//*****************************************************************************
// completion callback for microrl library
#ifdef _USE_COMPLETE
static char ** complete(void* user_handle, int argc, const char * const * argv) {
  (void)user_handle;

  int j = 0;
  int i = 0;

  compl_world[0] = NULL;

  // if there is token in cmdline
  if (argc == 1) {
    // get last entered token
    char * bit = (char*)argv [argc-1];
    // iterate through our available token and match it
    while (cliutils[i].name != NULL){
      if (strstr(cliutils[i].name, bit) == cliutils[i].name)
        compl_world[j++] = (char *)cliutils[i].name;
      i++;
    }
  }
  else { // if there is no token in cmdline, just print all available token
    while (cliutils[j].name != NULL){
      compl_world[j] = (char *)cliutils[j].name;
      j++;
    }
  }

  // note! last ptr in array always must be NULL!!!
  compl_world[j] = NULL;
  // return set of variants
  return compl_world;
}
#endif

/**
 *
 */
static void sigint (void* user_handle){
  (void)user_handle;

  if (current_cmd_tp != NULL){
    cli_print("^C pressed. Exiting...");
    chThdTerminate(current_cmd_tp);
    chThdWait(current_cmd_tp);
    current_cmd_tp = NULL;
    cli_print("--> Done. Press 'Enter' to return to shell");
  }
}

/**
 * Thread function
 */
static THD_WORKING_AREA(ShellThreadWA, 2048);
static THD_FUNCTION(ShellThread, sdp) {

  chRegSetThreadName("Shell");

  /* init static pointer for serial driver with received pointer */
  ShellSDp = (SerialDriver *)sdp;

  // create and init microrl object
  microrl_t microrl_shell;
  chThdSleepMilliseconds(10);
  cli_print("Mobile Operational System Kamize (MOSK) welcomes you.");
  cli_print(ENDL);
  chThdSleepMilliseconds(10);
  cli_print("Press enter to get command prompt.");
  microrl_init(&microrl_shell, NULL, microrl_print);

  // set callback for execute
  microrl_set_execute_callback(&microrl_shell, execute);

  // set callback for completion (optionally)
  microrl_set_complete_callback(&microrl_shell, complete);

  // set callback for ctrl+c handling (optionally)
  microrl_set_sigint_callback(&microrl_shell, sigint);

  while (!chThdShouldTerminateX()){
    // put received char from stdin to microrl lib
    msg_t c = sdGetTimeout(ShellSDp, MS2ST(50));
    if (c != Q_TIMEOUT)
      microrl_insert_char(&microrl_shell, (char)c);

    /* if fork finished than collect allocated for it memory */
    if ((current_cmd_tp != NULL) && (current_cmd_tp->p_state == CH_STATE_FINAL)){
      chThdWait(current_cmd_tp);
      current_cmd_tp = NULL;
    }
  }

  /* умираем по всем правилам, не забываем убить потомков */
  if (current_cmd_tp != NULL){
    if (current_cmd_tp->p_state != CH_STATE_FINAL)
      chThdTerminate(current_cmd_tp);
    chThdWait(current_cmd_tp);
  }

  chThdExit(MSG_OK);
}

/**
 *
 */
static thread_t* help_clicmd(int argc, const char * const * argv, SerialDriver *sdp){
  (void)sdp;
  (void)argc;
  (void)argv;

  int32_t i = 0;

  cli_println("Use TAB key for completion, UpArrow for previous command.");
  cli_println("Available commands are:");
  cli_println("-------------------------------------------------------------");

  while(cliutils[i].name != NULL){
    cli_print(cliutils[i].name);
    cli_print(" - ");
    cli_println(cliutils[i].help);
    i++;
  }

  return NULL;
}

/*
 *******************************************************************************
 * EXPORTED FUNCTIONS
 *******************************************************************************
 */

/**
 * Print routine for microrl.
 */
void cli_print(const char *str){
  int i = 0;
  while (str[i] != 0) {
    sdPut(ShellSDp, str[i]);
    i++;
  }
}

/**
 * Convenience function
 */
void cli_println(const char *str){
  cli_print(str);
  cli_print(ENDL);
}

/**
 * Convenience function
 */
void cli_put(char chr){
  sdPut(ShellSDp, chr);
}

/**
 * Read routine
 */
char get_char (void){
  return sdGet(ShellSDp);
}

/**
 * helper function
 * Inserts new line symbol if passed string does not contain NULL termination.
 * Must be used in combination with snprintf() function.
 */
void cli_print_long(const char * str, int n, int nres){
  cli_print(str);
  if (nres > n)
    cli_print(ENDL);
}

/**
 *
 */
void KillShellThreads(void) {
  if (NULL != shell_tp) {
    chThdTerminate(shell_tp);
    chThdWait(shell_tp);
    shell_tp = NULL;
  }
}

/**
 *
 */
void SpawnShellThreads(void *sdp) {

  shell_tp = chThdCreateStatic(ShellThreadWA,
                            sizeof(ShellThreadWA),
                            NORMALPRIO,
                            ShellThread,
                            sdp);
  if (shell_tp == NULL)
    osalSysHalt("Can not allocate memory");
}
