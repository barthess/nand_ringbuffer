#include "nand_test.hpp"

static uint8_t data[2048]; // TODO: delete hardcoded size

static NandWorker wrk;

static void direct_write_test(NandRing &ring) {
  ring.mount();
  ring.writePage(data);
  ring.umount();
}

static void async_write_test(NandRing &ring) {
  ring.mount();
  wrk.start(&ring);
  wrk.append(data, 1);
  wrk.stop();
  ring.umount();
}

void NandTest(NandRing &ring) {
  direct_write_test(ring);
  async_write_test(ring);
}

