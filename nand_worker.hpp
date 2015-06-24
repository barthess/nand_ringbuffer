#ifndef NAND_WORKER_HPP_
#define NAND_WORKER_HPP_

#include "ch.hpp"
#include "multi_buffer.hpp"
#include "nand_ring.hpp"

#define NAND_BUFFER_COUNT       3

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

#endif /* NAND_WORKER_HPP_ */
