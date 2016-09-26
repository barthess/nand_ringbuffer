#ifndef LINETEST_PROTO_H_
#define LINETEST_PROTO_H_

#define USE_HARDWARE_CRC    FALSE

#if USE_HARDWARE_CRC
#include "hal_crchw.h"
#endif

#define LINETEST_MAX_PAYLOAD_LEN    4096U
#define LINETEST_SYNC_LEN           4U
#define LINETEST_SEQUENCE_LEN       2U
#define LINETEST_SIZE_LEN           2U
#define LINETEST_CHECKSUM_LEN       4U

#define LINETEST_HEADER_LEN         (LINETEST_SYNC_LEN + \
                                     LINETEST_SEQUENCE_LEN + \
                                     LINETEST_SIZE_LEN)

#define LINETEST_OVERHEAD           (LINETEST_HEADER_LEN + LINETEST_CHECKSUM_LEN)

#define LINETEST_PARSER_BUF_SIZE    (LINETEST_MAX_PAYLOAD_LEN + LINETEST_OVERHEAD)

/**
 *
 */
typedef enum {
  LINETEST_COLLECT_HEADER_55,
  LINETEST_COLLECT_HEADER_AA,
  LINETEST_COLLECT_HEADER_FF,
  LINETEST_COLLECT_HEADER_00,
  LINETEST_COLLECT_SEQUENCE_0,
  LINETEST_COLLECT_SEQUENCE_1,
  LINETEST_COLLECT_SIZE_0,
  LINETEST_COLLECT_SIZE_1,
  LINETEST_COLLECT_DATA,
  LINETEST_COLLECT_CHECKSUM_0,
  LINETEST_COLLECT_CHECKSUM_1,
  LINETEST_COLLECT_CHECKSUM_2,
  LINETEST_COLLECT_CHECKSUM_3
} linetest_parserstate_t;

/**
 *
 */
typedef struct {
  uint16_t bad_checksum;
  uint16_t sequence_error;
  uint16_t oversize;
  uint32_t recvd_msgs;
  uint32_t total_bytes;
  uint32_t good_bytes;
} LinetestParserStats_t;

/**
 *
 */
typedef struct {
  uint16_t                  prev_sequence;
  uint16_t                  sequence;
  uint16_t                  size;
  uint32_t                  checksum;
  size_t                    tip;
  size_t                    datacnt;
  uint8_t                   buf[LINETEST_PARSER_BUF_SIZE];
  linetest_parserstate_t    state;
  LinetestParserStats_t     dbg;
} LinetestParser;

#ifdef __cplusplus
extern "C" {
#endif
  void LinetestParserObjectInit(LinetestParser *ctx);
  bool LinetestParserCollect(LinetestParser *ctx, uint8_t byte);
  const uint8_t* LinetestParserFill(LinetestParser *ctx, uint16_t len);
  void LinetestParserStats(const LinetestParser *ctx, LinetestParserStats_t *result);
#ifdef __cplusplus
}
#endif

#endif /* LINETEST_PROTO_H_ */
