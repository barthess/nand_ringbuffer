#ifndef NAND_RING_TEST_H_
#define NAND_RING_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif
  void nandRingTest(NANDDriver *nandp, const NANDConfig *config, bitmap_t *bb_map);
  void nandRingIteratorTest(NANDDriver *nandp, const NANDConfig *config, bitmap_t *bb_map);
#ifdef __cplusplus
}
#endif

#endif /* NAND_RING_TEST_H_ */
