#include "ch.h"
#include "hal.h"

static systime_t prev_time = 0;
static uint32_t wrap_cnt = 0;
static const uint64_t overlap_size = (uint64_t)1 << (sizeof(systime_t) * 8);

/**
 *
 */
uint64_t timebootU64(void) {

  osalSysLock();
  systime_t now = chVTGetSystemTimeX();
  if (prev_time > now) {
    wrap_cnt++;
  }
  prev_time = now;
  uint64_t tmp = overlap_size * wrap_cnt + now;
  osalSysUnlock();

  return (tmp * 1000000 + CH_CFG_ST_FREQUENCY - 1) / CH_CFG_ST_FREQUENCY;
}

