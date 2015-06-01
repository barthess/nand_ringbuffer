#ifndef NAND_RING_HPP_
#define NAND_RING_HPP_

#include "ch.hpp"
#include "multi_buffer.hpp"

#define NAND_BUFFER_COUNT       3

/**
 *
 */
struct NandPageHeader {
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
   * @brief     Correction for system boot time stamp
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
   * @details   Increments on every file system mount. Used for quicker
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
  bool mkfs(void);
  size_t append(const uint8_t *data, size_t len);
  void setUtcCorrection(uint32_t correction);
  size_t searchSessions(Session *result, size_t max_sessions);
private:
  friend void NandWorker(void *arg);
  uint32_t next_good(uint32_t current);
  uint32_t get_total_good(void);
  uint8_t read_session_num(uint32_t blk, uint32_t page);
  void flush(const uint8_t *data);
  MultiBufferAccumulator<2048, NAND_BUFFER_COUNT> multibuf; //FIXME: remove hardcoded sizes
  chibios_rt::Mailbox<uint8_t*, NAND_BUFFER_COUNT> mailbox;
  uint32_t current_blk;
  uint32_t current_page;
  uint64_t current_id;
  uint8_t current_session;
  uint32_t utc_correction;
  const uint32_t start; // first block of storage
  const uint32_t end;   // last block of storage
  NANDDriver *nandp;
  uint8_t *sparebuf;
  thread_t *worker;
  bool ready;
  uint32_t total_good_blk; // mostly for diagnostics using console
};

#endif /* NAND_RING_HPP_ */
