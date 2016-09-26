#ifndef NAND_LOG_TEST_H_
#define NAND_LOG_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif
  void nandLogTest(NANDDriver *nandp, const NANDConfig *config, bitmap_t *bb_map);
#ifdef __cplusplus
}
#endif

#endif /* NAND_LOG_TEST_H_ */
