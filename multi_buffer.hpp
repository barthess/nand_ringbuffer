#ifndef MULTI_BUFFER_HPP_
#define MULTI_BUFFER_HPP_

#include "ch.hpp"

template <typename T, size_t L, size_t Count>
class MultiBuffer {
public:

  /**
   *
   */
  MultiBuffer(void) {
    constructor_impl(0);
  }

  /**
   *
   */
  T *next(void) {
    head++;
    if (head == Count)
      head = 0;
    return &(internal_buf[head * L]);
  }

  /**
   *
   */
  T *current(void) {
    return &(internal_buf[head * L]);
  }

  /**
   * @brief     Return size of of single buffer.
   */
  size_t size(void) {
    return L * sizeof(T);
  }

private:
  /**
   *
   */
  void constructor_impl(int pattern) {
    static_assert(Count > 1, "Multibuffer with 1 count value is pointless");
    memset(internal_buf, pattern, sizeof(internal_buf));
    head = 0;
  }

  T internal_buf[Count * L];
  size_t head;
};





template <size_t L, size_t Count>
class MultiBufferAccumulator2 {
public:
  MultiBufferAccumulator2(void) {
    static_assert(Count > 1, "Multibuffer with 1 count value is pointless");
    static_assert(L >= sizeof(void*), "Objects pool can not handle such small objecst");
    current = (uint8_t *)this->pool.alloc();
    osalDbgAssert(nullptr != current, "Can not allocate memory");
  }

  /**
   * @brief     Append data portion to the buffer.
   *
   * @retval    Pointer to full buffer. @p NULL if buffer has some free space yet.
   */
  uint8_t *append(const uint8_t *data, size_t len, size_t *written) {
    const size_t free = get_free_space();
    uint8_t *ret;

    /* stupidity protection */
    osalDbgCheck(len <= L);

    /* try to allocate new memory block */
    if (nullptr == current) {
      current = this->pool.alloc();
      head = 0;
      /* no blocks available */
      if (nullptr == current) {
        *written = 0;
        return nullptr;
      }
    }

    /* now store data */
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
      current = this->pool.alloc();
      if (nullptr != current) {
        memcpy(current, data+free, remainder);// second to the beginning of the next buffer
        head = remainder;
        *written += remainder;
      }
    }
    else { /* free == len */
      memcpy(current, data, len);
      ret = current;
      current = nullptr; // allocation of new block will be done on next fucntion call
      head += len;
      *written = len;
    }

    return ret;
  }

  /**
   * @brief   Return memory block to pool
   * @param   Pointer to unneded meory block
   */
  void free(uint8_t *p) {
    this->pool.free(p);
  }

private:
  size_t get_free_space(void) {
    return L - head;
  }

  chibios_rt::ObjectsPool<uint8_t[L], Count> pool;
  uint8_t *current = nullptr;
  size_t head = 0;
};






/**
 * @brief   Convenient class allowing to add portions of data to the
 *          multiple buffer without gaps on the buffers' boundaries.
 */
template <typename T, size_t L, size_t Count>
class MultiBufferAccumulator {
public:
  /**
   *
   */
  MultiBufferAccumulator(void) {
    tip = multi_buffer.current();
  }

  /**
   * @brief     Append data portion to the buffer.
   *
   * @retval    Pointer to full buffer. @p NULL if buffer has some free space yet.
   */
  T *append(const T *data, size_t len) {
    const size_t free = get_free_space();
    T *ret;

    /* stupidity protection */
    osalDbgCheck(len <= multi_buffer.size());

    if (free > len) {
      memcpy(tip, data, len);
      tip += len;
      ret = nullptr;
    }
    else if (free < len) {
      const size_t remainder = len - free;
      memcpy(tip, data, free);// first portion to the end of current buffer
      ret = multi_buffer.current();
      tip = multi_buffer.next();
      memcpy(tip, data+free, remainder);// second to the beginning of the next buffer
      tip += remainder;
    }
    else { /* free == len */
      memcpy(tip, data, len);
      ret = multi_buffer.current();
      tip = multi_buffer.next();
    }

    return ret;
  }

  /**
   * @brief     Return sizeof of single buffer.
   */
  size_t size(void) {
    return multi_buffer.size();
  }

private:
  /**
   * @brief     Get available space in current working buffer.
   */
  size_t get_free_space(void) {
    return multi_buffer.size() - (tip - multi_buffer.current());
  }

  MultiBuffer<T, L, Count> multi_buffer;
  T *tip;
};

#endif /* MULTI_BUFFER_HPP_ */

