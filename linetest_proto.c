#include <string.h>
#include <stdlib.h>

#include "ch.h"
#include "hal.h"

#include "linetest_proto.h"

/*
 ******************************************************************************
 * DEFINES
 ******************************************************************************
 */
#if ! USE_HARDWARE_CRC
#define OEM6_CRC32_POLY     0xEDB88320
#endif

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
#if USE_HARDWARE_CRC
static const CRCHWConfig crc_oem6_cfg = {
    0x04C11DB7,
    32,
    true,
    true
};
#endif

/*
 ******************************************************************************
 ******************************************************************************
 * LOCAL FUNCTIONS
 ******************************************************************************
 ******************************************************************************
 */

#if USE_HARDWARE_CRC
/**
 *
 */
static uint32_t calc_block_crc32(const uint8_t *ucBuffer, uint32_t ulCount) {

  crchwAcquireBus(&CRCHWD1);
  uint32_t ret = crchwCalc(&CRCHWD1, ucBuffer, ulCount, 0);
  crchwReleaseBus(&CRCHWD1);
  return ret;
}
#else /* USE_HARDWARE_CRC */
/**
 *
 */
static uint32_t crc32_val(int32_t i) {
    int8_t j;
    uint32_t ulCRC = i;

    for ( j = 8 ; j > 0; j-- ) {
        if ( ulCRC & 1 ) {
            ulCRC = ( ulCRC >> 1 ) ^ OEM6_CRC32_POLY;
        } else {
            ulCRC >>= 1;
        }
    }

    return ulCRC;
}

/**
 *
 */
static uint32_t calc_block_crc32(const uint8_t *ucBuffer, uint32_t ulCount) {
    uint32_t ulTemp1;
    uint32_t ulTemp2;
    uint32_t ulCRC = 0;
    while ( ulCount-- != 0 ) {
        ulTemp1 = ( ulCRC >> 8 ) & 0x00FFFFFFL;
        ulTemp2 = crc32_val(((int32_t) ulCRC ^ *ucBuffer++ ) & 0xff );
        ulCRC = ulTemp1 ^ ulTemp2;
    }
    return( ulCRC );
}
#endif /* USE_HARDWARE_CRC */

/**
 *
 */
static void reset_parser(LinetestParser *ctx) {
  ctx->state = LINETEST_COLLECT_HEADER_55;
  ctx->tip = 0;
  ctx->datacnt = 0;
}

/**
 *
 */
static void push(LinetestParser *ctx, uint8_t byte) {
  ctx->buf[ctx->tip] = byte;
  ctx->tip++;
}

/**
 *
 */
static void fill_rand(uint8_t *buf, uint16_t len) {
  uint32_t r;

  while (len > 3) {
    r = rand();
    memcpy(buf, &r, 4);
    buf += 4;
    len -= 4;
  }

  r = rand();
  memcpy(buf, &r, len);
}

/*
 ******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************
 */
/**
 *
 */
void LinetestParserObjectInit(LinetestParser *ctx) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->state = LINETEST_COLLECT_HEADER_55;
  ctx->prev_sequence = ctx->sequence - 1;

#if USE_HARDWARE_CRC
  crchwObjectInit(&CRCHWD1);
  crchwStart(&CRCHWD1, &crc_oem6_cfg);
#endif
}

/**
 *
 */
bool LinetestParserCollect(LinetestParser *ctx, uint8_t c) {
  bool ret = false;
  uint32_t checksum;
  uint16_t delta;

  ctx->dbg.total_bytes++;

  switch(ctx->state) {
  case LINETEST_COLLECT_HEADER_55:
    if (0x55 == c) {
      push(ctx, c);
      ctx->state = LINETEST_COLLECT_HEADER_AA;
    }
    else {
      reset_parser(ctx);
    }
    break;

  case LINETEST_COLLECT_HEADER_AA:
    if (0xAA == c) {
      push(ctx, c);
      ctx->state = LINETEST_COLLECT_HEADER_FF;
    }
    else {
      reset_parser(ctx);
    }
    break;

  case LINETEST_COLLECT_HEADER_FF:
    if (0xFF == c) {
      push(ctx, c);
      ctx->state = LINETEST_COLLECT_HEADER_00;
    }
    else {
      reset_parser(ctx);
    }
    break;

  case LINETEST_COLLECT_HEADER_00:
    if (0x00 == c) {
      push(ctx, c);
      ctx->state = LINETEST_COLLECT_SEQUENCE_0;
    }
    else {
      reset_parser(ctx);
    }
    break;

  case LINETEST_COLLECT_SEQUENCE_0:
    push(ctx, c);
    ctx->sequence = c;
    ctx->state = LINETEST_COLLECT_SEQUENCE_1;
    break;

  case LINETEST_COLLECT_SEQUENCE_1:
    push(ctx, c);
    ctx->sequence |= (uint16_t)c << 8;
    delta = ctx->sequence - ctx->prev_sequence;
    if (delta != 1) {
      ctx->dbg.sequence_error++;
    }
    ctx->prev_sequence = ctx->sequence;
    ctx->state = LINETEST_COLLECT_SIZE_0;
    break;

  case LINETEST_COLLECT_SIZE_0:
    push(ctx, c);
    ctx->size = c;
    ctx->state = LINETEST_COLLECT_SIZE_1;
    break;

  case LINETEST_COLLECT_SIZE_1:
    push(ctx, c);
    ctx->size |= (uint16_t)c << 8;
    if (ctx->size > LINETEST_MAX_PAYLOAD_LEN) {
      ctx->dbg.oversize++;
      reset_parser(ctx);
    }
    else if (0 == ctx->size) {
      ctx->state = LINETEST_COLLECT_CHECKSUM_0;
    }
    else {
      ctx->state = LINETEST_COLLECT_DATA;
    }
    break;

  case LINETEST_COLLECT_DATA:
    push(ctx, c);
    ctx->datacnt++;
    if (ctx->datacnt == ctx->size) {
      ctx->state = LINETEST_COLLECT_CHECKSUM_0;
    }
    break;

  case LINETEST_COLLECT_CHECKSUM_0:
    push(ctx, c);
    ctx->state = LINETEST_COLLECT_CHECKSUM_1;
    break;

  case LINETEST_COLLECT_CHECKSUM_1:
    push(ctx, c);
    ctx->state = LINETEST_COLLECT_CHECKSUM_2;
    break;

  case LINETEST_COLLECT_CHECKSUM_2:
    ctx->state = LINETEST_COLLECT_CHECKSUM_3;
    push(ctx, c);
    break;

  case LINETEST_COLLECT_CHECKSUM_3:
    push(ctx, c);
    checksum = calc_block_crc32(ctx->buf, ctx->size + LINETEST_HEADER_LEN);
    if (0 != memcmp(&checksum, &ctx->buf[ctx->size + LINETEST_HEADER_LEN], 4)) {
      ctx->dbg.bad_checksum++;
    }
    else {
      ctx->dbg.recvd_msgs++;
      ctx->dbg.good_bytes += ctx->tip;
      ret = true;
    }
    reset_parser(ctx);
    break;
  }

  return ret;
}

/**
 *
 */
const uint8_t* LinetestParserFill(LinetestParser *ctx, uint16_t len) {
  const uint32_t header = 0x00FFAA55;
  void *ptr = ctx->buf;

  memcpy(ptr, &header, 4);
  ptr += 4;
  memcpy(ptr, &ctx->sequence, 2);
  ctx->sequence++;
  ptr += 2;
  memcpy(ptr, &len, 2);
  ctx->size = len;
  ptr += 2;

  fill_rand(ptr, len);
  ptr += len;
  const uint32_t checksum = calc_block_crc32(ctx->buf, LINETEST_HEADER_LEN + len);
  memcpy(ptr, &checksum, 4);

  return ctx->buf;
}

/**
 *
 */
void LinetestParserStats(const LinetestParser *ctx, LinetestParserStats_t *result) {
  memcpy(result, &ctx->dbg, sizeof(ctx->dbg));
}


