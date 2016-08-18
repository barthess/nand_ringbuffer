#ifndef SOFT_CRC_H_
#define SOFT_CRC_H_

#ifdef __cplusplus
extern "C" {
#endif
  uint32_t softcrc32(const uint8_t *buf, size_t len, uint32_t crc);
  uint8_t softcrc8(const uint8_t *buf, size_t len, uint8_t crc);
#ifdef __cplusplus
}
#endif

#endif /* SOFT_CRC_H_ */
