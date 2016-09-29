#ifndef NAND_LOG_H_
#define NAND_LOG_H_

#include "nand_ring.h"

#define NAND_BUFFER_COUNT       3

typedef enum {
  NAND_LOG_UNINIT,
  NAND_LOG_READY,
  NAND_LOG_NO_SPACE,
  NAND_LOG_STOP
} nand_log_state_t;

/**
 *
 */
typedef struct {
  NandRing          *ring;
  thread_t          *worker;
  nand_log_state_t  state;

  mailbox_t         mb;
  msg_t             mailbox_buf[NAND_BUFFER_COUNT];

  size_t            bfree;
  uint8_t           *btip;
  memory_pool_t     mempool;
  uint8_t           *mempool_buf;
} NandLog;


#ifdef __cplusplus
extern "C" {
#endif
  void nandLogObjectInit(NandLog *log);
  void nandLogStart(NandLog *log, NandRing *ring,
                    const NandRingConfig *nandringcfg,
                    uint8_t *ring_working_area);
  size_t nandLogWrite(NandLog *log, const uint8_t *data, size_t len);
  void nandLogErase(NandLog *log);
  void nandLogStop(NandLog *log);
#ifdef __cplusplus
}
#endif

#if 0
/**
 *
 */
class NandWorker {
public:
  NandWorker(void);
  void start(NandRing *ring);
  void stop(void);
  size_t append(const uint8_t *data, size_t len);
private:
  THD_WORKING_AREA(NandWorkerThreadWA, 320);
  static THD_FUNCTION(WorkerThread, arg);
  MultiBufferAccumulator<2048, NAND_BUFFER_COUNT> multibuf; //FIXME: remove hardcoded sizes
  chibios_rt::Mailbox<uint8_t*, NAND_BUFFER_COUNT> mailbox;
  bool ready;
  NandRing *ring;
  thread_t *worker;
};
#endif

#endif /* NAND_LOG_H_ */
