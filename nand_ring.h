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
  uint16_t  bad_mark;
  /**
   * @brief     Page id. Monotonically increasing numbers.
   * @details   Zero value reserved.
   */
  uint64_t  id;
  /**
   * @brief     Microseconds since system boot
   */
  uint64_t  time_boot_uS;
  /**
   * @brief     Correction for system boot time stamp
   * @details   Used when no date/time was available during boot and was
   *            acquired later, for example from GPS.
   */
  uint32_t  utc_correction;
  /**
   * @brief     Page ECC data.
   */
  uint32_t  page_ecc;
  /**
   * @brief     Session number.
   * @details   Increments on every file system mount. Used for quicker
   *            session search.
   */
  //uint8_t   session;
  /**
   * @brief     Seal CRC for this structure
   */
  uint8_t   spare_crc;
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
  NAND_RING_STOP
} nand_ring_state_t;

/**
 *
 */
typedef struct {
  const uint32_t  start_blk; // first block of storage
  const uint32_t  end_blk;   // last block of storage
  NANDDriver      *nandp;
} NandRingConfig;

/**
 *
 */
typedef struct {
  const NandRingConfig  *config;
  uint32_t              cur_blk;
  uint32_t              cur_page;
  uint64_t              cur_id;
  //uint8_t               cur_session;
  uint32_t              utc_correction;
  nand_ring_state_t     state;
} NandRing;

#ifdef __cplusplus
extern "C" {
#endif
  void nandRingObjectInit(NandRing *ring);
  void nandRingStart(NandRing *ring, const NandRingConfig *config);
  bool nandRingMount(NandRing *ring);
  void nandRingWritePage(NandRing *ring, const uint8_t *data);
  void nandRingStop(NandRing *ring);
  void nandRingSetUtcCorrection(uint32_t correction);
  size_t nandRingSearchSessions(RingSession *result, size_t max_sessions);
#ifdef __cplusplus
}
#endif

#endif /* NAND_RING_H_ */
