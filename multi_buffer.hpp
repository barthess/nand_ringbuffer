#ifndef MULTI_BUFFER_HPP_
#define MULTI_BUFFER_HPP_

#include "ch.hpp"

/**
 *
 */
template <size_t L, size_t Count>
class MultiBufferAccumulator {
public:
  MultiBufferAccumulator(void) {
    static_assert(Count > 1, "Multibuffer with 1 count value is pointless");
    static_assert(L >= sizeof(void*), "Objects pool can not handle such small objects");
    current = (uint8_t *)this->pool.alloc();
    osalDbgAssert(nullptr != current, "Can not allocate memory");
  }

  /**
   * @brief     Append data portion to the buffer.
   *
   * @retval    Pointer to full buffer. @p NULL if buffer has some free space yet.
   */
  uint8_t *append(const uint8_t *data, size_t len, size_t *written) {
    uint8_t *ret;

    /* stupidity protection */
    osalDbgCheck(len <= L);

    /* try to allocate new memory block */
    if (nullptr == current) {
      current = static_cast<uint8_t*>(this->pool.alloc());
      head = 0;
      /* no blocks available */
      if (nullptr == current) {
        *written = 0;
        return nullptr;
      }
    }

    /* now store data */
    const size_t free = get_free_space();
    if (free > len) {
      memcpy(current, data, len);
      head += len;
      ret = nullptr;
      *written = len;
    }
    else if (free < len) {
      const size_t remainder = len - free;
      memcpy(current, data, free);// first portion to the end of current buffer
      head += free;
      *written = free;
      ret = current;
      current = static_cast<uint8_t*>(this->pool.alloc());
      if (nullptr != current) {
        memcpy(current, data+free, remainder);// second to the beginning of the next buffer
        head = remainder;
        *written += remainder;
      }
    }
    else { /* free == len */
      memcpy(current, data, len);
      ret = current;
      current = nullptr; // allocation of new block will be done on next function call
      head += len;
      *written = len;
    }

    return ret;
  }

  /**
   * @brief   Return memory block to the pool
   * @param   Pointer to unneeded memory block
   */
  void free(uint8_t *p) {
    this->pool.free(p);
  }

private:
  size_t get_free_space(void) {
    osalDbgCheck(head <= L);
    return L - head;
  }

  chibios_rt::ObjectsPool<uint8_t[L], Count> pool;
  uint8_t *current = nullptr;
  size_t head = 0;
};

#endif /* MULTI_BUFFER_HPP_ */

