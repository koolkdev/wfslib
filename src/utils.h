/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <bit>
#include <cassert>

template <typename T>
class big_endian_type {
 public:
  // for static_assert
  constexpr static bool always_false = false;

  big_endian_type() = default;

  explicit big_endian_type(T val) { *data() = convert_endian(val); }

  big_endian_type& operator=(T val) {
    *data() = convert_endian(val);
    return *this;
  }

  T value() const { return convert_endian(*data()); }

 private:
  inline static T convert_endian(T val) {
    if constexpr (std::endian::native == std::endian::little)
      return std::byteswap(val);
    else if constexpr (std::endian::native == std::endian::big)
      return val;
    else
      static_assert(always_false);
  }

  const T* data() const { return reinterpret_cast<const T*>(value_); }
  T* data() { return reinterpret_cast<T*>(value_); }

  // Use char array to make the object unaligned
  char value_[sizeof(T)];
};

typedef big_endian_type<uint8_t> uint8_be_t;
typedef big_endian_type<uint16_t> uint16_be_t;
typedef big_endian_type<uint32_t> uint32_be_t;
typedef big_endian_type<uint64_t> uint64_be_t;

inline size_t align_to_power_of_2(size_t size) {
  assert(size != 0);
  return static_cast<size_t>(1ULL << std::bit_width(size - 1));
}

inline size_t div_ceil(size_t n, size_t div) {
  return (n + div - 1) / div;
}

template <typename T>
constexpr const T* as_const(T* obj) {
  return obj;
}
