#ifndef NAND_RING_HPP_
#define NAND_RING_HPP_

#include "ch.hpp"
#include "multi_buffer.hpp"

/**
 *
 */
struct SpareFormat {
  /**
   * @brief     NAND specific area for bad mark storing.
   * @details   Must be always set to 0xFFFF i.e. erased.
   */
  uint16_t  bad_mark = -1;
  /**
   * @brief     Page id. Monotonically increasing numbers.
   * @details   Zero value reserved.
   */
  uint64_t  id = 1;
  /**
   * @brief     Microseconds since system boot
   */
  uint64_t  time_boot_uS = 0;
  /**
   * @brief     Correction for system boot timestamp
   * @details   Used when no date/time was available during boot and was
   *            acquired later, for example from GPS.
   */
  uint32_t  utc_correction = 0;
  /**
   * @brief     Page ECC data.
   */
  uint32_t  page_ecc;
  /**
   * @brief     Session number.
   * @details   Increments on every filesystem mount. Used for quicker
   *            session search.
   */
  uint8_t   session = 0;
  /**
   * @brief     Seal CRC for this structure
   */
  uint8_t   spare_crc = -1;
} __attribute__((packed));

/**
 *
 */
struct Session {
  uint16_t  blk_start;
  uint16_t  page_start;
  uint32_t  utc_start;
  uint16_t  blk_end;
  uint16_t  page_end;
  uint32_t  utc_end;
};

/**
 *
 */
class NandRing {
public:
  NandRing(NANDDriver *nandp, uint32_t start_blk, uint32_t end_blk);
  bool mount(void);
  bool format(void);
  bool append(const uint8_t *data, size_t len);
  void setUtcCorrection(uint32_t correction);
  size_t searchSessions(Session *result, size_t max_sessions);
private:
  friend void NandWorker(void *arg);
  uint32_t next_good(uint32_t current);
  void flush(const uint8_t *data);
  MultiBufferAccumulator<uint8_t, 2048, 3> multibuf;//FIXME: remove hardcoded sizes
  MultiBufferAccumulator2<2048, 3> pool; //FIXME: remove hardcoded sizes
  chibios_rt::Mailbox<uint8_t *, 3> mailbox; // FIXME: remove hardcoded size
  uint32_t current_blk;
  uint32_t current_page;
  uint64_t current_id;
  uint32_t utc_correction;
  const uint32_t start_blk;
  const uint32_t end_blk;
  NANDDriver *nandp;
  uint8_t *sparebuf;
  thread_t *worker;
};

#endif /* NAND_RING_HPP_ */
