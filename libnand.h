#ifndef LIBNAND_H_
#define LIBNAND_H_

#define NAND_STATUS_FAILED      0x1
#define NAND_STATUS_SUCCESS     0x0

#ifdef __cplusplus
extern "C" {
#endif
  uint32_t __nandEraseRangeForce(NANDDriver *nandp, uint32_t start, uint32_t len);
  void __nandSetErrorChance(uint32_t chance);
  uint32_t nandEraseRange(NANDDriver *nandp, uint32_t start, uint32_t len);
  uint32_t nandFillRandomRange(NANDDriver *nandp, uint32_t start,
                               uint32_t len, void *pagebuf);
  uint8_t nandDataMove(NANDDriver *nandp, uint32_t src_blk,
                       uint32_t trgt_blk, uint32_t pages, uint8_t *working_area);
  bool nandFailed(uint8_t status);
#ifdef __cplusplus
}
#endif

#endif /* LIBNAND_H_ */
