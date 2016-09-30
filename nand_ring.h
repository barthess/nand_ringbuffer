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
   */
  uint32_t    utc_correction;
  /**
   * @brief     Page ECC data.
   */
  uint32_t    page_ecc;
  /**
   * @brief     Last block number in _previous_ session.
   */
  uint16_t    back_link;
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
  uint64_t id;                /* from first record */
  uint64_t time_boot_us;      /* from first record */
  uint32_t utc_correction;    /* from last record */
  uint16_t first_blk;
  uint16_t last_blk;          /* may be the same as first one */
  uint16_t last_page;
} NandRingSession;

/**
 *
 */
typedef enum {
  NAND_RING_UNINIT,
  NAND_RING_IDLE,
  NAND_RING_MOUNTED,
  NAND_RING_ITERATOR_BOUNDED,
  /* no good blocks left in ring */
  NAND_RING_NO_SPACE,
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
  uint64_t              cur_id;
  uint32_t              cur_blk;
  uint32_t              cur_page;
  uint32_t              utc_correction;
  uint16_t              cur_back_link;
  nand_ring_state_t     state;
  nand_ring_debug_t     dbg;
  const NandRingConfig  *config;
  /**
   * @brief   working area buffer
   * @details must be big enough to store page data + page spare
   */
  uint8_t               *wa;
} NandRing;

//typedef enum {
//  NAND_ITERATOR_NO_SESSION,
//  NAND_ITERATOR_SINGLE_SESSION,
//  NAND_ITERATOR_LOOPED_SESSION,
//  NAND_ITERATOR_MULTI_SESSION
//} nand_iterator_session_t;

/**
 *
 */
typedef struct {
  NandRing *ring;
  /**
   * @brief last written block of the current session
   */
  uint32_t last_blk;
  /**
   * @brief end if iteration flag
   */
  bool finished;
} NandRingIterator;


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
  void nandRingSetUtcCorrection(NandRing *ring, uint32_t correction);
  void NandRingIteratorBind(NandRingIterator *it, NandRing *ring);
  void NandRingIteratorRelease(NandRingIterator *it);
  bool NandRingIteratorFinished(NandRingIterator *it);
  bool NandRingIteratorNext(NandRingIterator *it, NandRingSession *session);
#ifdef __cplusplus
}
#endif

#endif /* NAND_RING_H_ */
