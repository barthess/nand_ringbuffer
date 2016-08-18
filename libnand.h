#ifndef LIBNAND_H_
#define LIBNAND_H_

#define NAND_STATUS_FAILED      0x1

#ifdef __cplusplus
extern "C" {
#endif
  uint32_t nandEraseRange(NANDDriver *nandp, uint32_t start, uint32_t len);
  uint32_t nandFillRandomRange(NANDDriver *nandp, uint32_t start,
                               uint32_t len, void *pagebuf);
#ifdef __cplusplus
}
#endif

#endif /* LIBNAND_H_ */
