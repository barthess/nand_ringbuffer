#ifndef NAND_RING_H_
#define NAND_RING_H_

/**
 *
 */
typedef struct __attribute__((packed)) {
  /**
   * @brief     NAND specific area for bad mark storing.
   * @details   Must be always set to 0xFFFF i.e. erased.
   */
  uint16_t    bad_mark;
  /**
   * @brief     Page id. Monotonically increasing numbers.
   * @details   Zero value reserved.
   */
  uint64_t    id;
  /**
   * @brief     Microseconds since system boot
   */
  uint64_t    time_boot_us;
  /**
   * @brief     Correction for system boot time stamp.
   * @details   Used when no date/time was available during boot and was
   *            acquired later, for example from GPS.
   * @note      Currently unused.
   */
  uint32_t    utc_correction;
  /**
   * @brief     Page ECC data.
   */
  uint32_t    page_ecc;
  /**
   * @brief     Session number.
   * @details   Increments on every file system mount. Used for quicker
   *            session search.
   * @note      Currently unused.
   */
  uint16_t    session;
  /**
   * @brief     Number of actually written bytes in page.
   * @details   Used during flush procedure and during "file" size estimation.
   * @note      Currently unused.
   */
  uint16_t    written;
  /**
   * @brief     Seal CRC for this structure
   */
  uint32_t    spare_crc;
} NandPageHeader;

/**
 *
 */
typedef struct {
  uint16_t  blk_start;
  uint16_t  page_start;
  uint32_t  utc_start;
  uint16_t  blk_end;
  uint16_t  page_end;
  uint32_t  utc_end;
} RingSession;

/**
 *
 */
typedef enum {
  NAND_RING_UNINIT,
  NAND_RING_IDLE,
  NAND_RING_MOUNTED,
  NAND_RING_NO_SPACE, /* no good blocks left in ring */
  NAND_RING_STOP
} nand_ring_state_t;

/**
 *
 */
typedef struct {
  uint32_t    start_blk;  // first block of storage
  size_t      len;        // length of ring in blocks
  NANDDriver  *nandp;
} NandRingConfig;

/**
 *
 */
typedef struct {
  uint32_t    data_rescue;
  uint32_t    new_badblocks;
  uint32_t    write_data_failed;
  uint32_t    write_spare_failed;
  uint32_t    erase_failed;
} nand_ring_debug_t;

/**
 *
 */
typedef struct {
  const NandRingConfig  *config;
  uint32_t              cur_blk;
  uint32_t              cur_page;
  uint64_t              cur_id;
  /**
   * @brief   working area buffer
   * @details must be big enough to store page data + page spare
   */
  uint8_t               *wa;
  nand_ring_state_t     state;
  nand_ring_debug_t     dbg;
} NandRing;

#ifdef __cplusplus
extern "C" {
#endif
  void nandRingObjectInit(NandRing *ring);
  void nandRingStart(NandRing *ring, const NandRingConfig *config, uint8_t *working_area);
  bool nandRingMount(NandRing *ring);
  uint32_t nandRingWASize(const NANDDriver *nandp);
  uint32_t nandRingTotalGood(const NandRing *ring);
  void nandRingUmount(NandRing *ring);
  bool nandRingWritePage(NandRing *ring, const uint8_t *data);
  void nandRingStop(NandRing *ring);
  void nandRingErase(NandRing *ring);
  void nandRingSetUtcCorrection(uint32_t correction);
  size_t nandRingSearchSessions(RingSession *result, size_t max_sessions);
#ifdef __cplusplus
}
#endif

#endif /* NAND_RING_H_ */
