#ifndef NAND_RING_HPP_
#define NAND_RING_HPP_

#include "ch.hpp"
#include "multi_buffer.hpp"

/**
 *
 */
struct PageHeader {
  uint16_t  bad_mark = -1;
  uint64_t  id = 0;
  uint64_t  time_boot_uS = 0;
  uint32_t  utc_correction;
  uint32_t  page_ecc;
  uint8_t   spare_crc;
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
  MultiBufferAccumulator<uint8_t, 2048, 3> multibuf; //FIXME: remove hardcoded sizes
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
