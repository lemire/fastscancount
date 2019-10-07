#ifndef FASTSCANCOUNT_AVX512_H
#define FASTSCANCOUNT_AVX512_H

// this code expects an x64 processor with AVX-512F

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>

namespace fastscancount {
namespace {

// credit: inspired by 256-bit implementation of Travis Downes
void populate_hits_avx512(std::vector<uint8_t> &counters, size_t range,
                       size_t threshold, size_t start,
                       std::vector<uint32_t> &out) {
  uint8_t *array = counters.data();

  size_t vsize = range / 64;
  __m512i *varray = (__m512i *)array;
  const __m512i comprand = _mm512_set1_epi8(threshold);

  for (size_t i = 0; i < vsize; i++) {
    size_t start_add = start + i*64;
    __m512i v = _mm512_loadu_si512(varray + i);
    uint64_t bits = _mm512_cmpgt_epi8_mask(v, comprand);
    while (bits) {
      unsigned zqty = __builtin_ctzll(bits);
      bits >>= zqty; 
      bits >>= 1; // If zqty = 63, shift by 64 is not defined, need to split shifts
      out.push_back(start_add + zqty);
      start_add += zqty + 1;
    }
  }

  for (size_t i = vsize * 64; i < range; i++) {
    auto v = array[i];
    if (v > threshold)
      out.push_back(start + i);
  }

}

void update_counters_avx512(const uint32_t  *&it_, const uint32_t  *end,
                            uint8_t *counters, 
                            const size_t shift) {

  if (it_ > end) {
    throw std::runtime_error("Bug: start > end");
  }
  size_t qty = end - it_;
  size_t vsize = qty / 16;

  __m512i *varray = (__m512i *)it_;
  const __m512i add1 = _mm512_set1_epi32(1);
  const __m512i shift_vect = _mm512_set1_epi32(shift);

  const __mmask64 blend_mask = 0x1111111111111111ull;

  for (unsigned i = 0; i < vsize; ++i) {
    __m512i indx = _mm512_sub_epi32(_mm512_loadu_si512(varray + i), shift_vect);
    __m512i v_orig = _mm512_i32gather_epi32(indx, (const int*)counters, 1);
    // Note: works correctly only if counters never overflow
    // First, we increment counters.
    __m512i v_inc = _mm512_add_epi32(v_orig, add1);
    // Then, we will blend by keeping three higher-order bytes in each 32-bit word unmodified
    // When 32-bit words overlap, the gather operation would first write the old values of the word
    // then it will overwrite them with new values. So, this should work just fine.
    __m512i v = _mm512_mask_blend_epi8(blend_mask, v_orig, v_inc);
    _mm512_i32scatter_epi32((int*)counters, indx, v, 1);
  }

  // tail processing
  const uint32_t  *it = it_ + vsize * 16;
  for (; it != end; it++) {
    counters[*it-shift]++;
  }
  it_ = end;
}


} // namespace

void fastscancount_avx512(uint32_t cache_size,
                          const std::vector<const std::vector<uint32_t>*> &data,
                          const std::vector<const std::vector<uint32_t>*> &range_ends,
                          std::vector<uint32_t> &out, uint8_t threshold) {
  std::vector<uint8_t> counters(cache_size);
  out.clear();
  const size_t dsize = data.size();
  if (!dsize) {
    return;
  }
  if (dsize != range_ends.size()) {
    throw std::runtime_error("Invalid input: non-matching sizes between data and range_ends");
  }

  unsigned range_qty = range_ends[0]->size();
  for (unsigned i = 1; i < dsize; ++i) {
    if (range_ends[i]->size() != range_qty) {
      throw std::runtime_error("Invalid input: different range sizes for different data arrays!");
    }
  }

  auto cdata = counters.data();

  std::vector<const uint32_t*> it(dsize);
  for (unsigned k = 0; k < dsize; ++k) {
    const auto& v = *data[k];  
    if (!v.empty()) {
      it[k] = &v[0];
    }
  }

  for (unsigned i = 0; i < range_qty; ++i) {
    memset(cdata, 0, cache_size * sizeof(counters[0]));
    uint32_t start = i * cache_size;
    for (unsigned k = 0; k < dsize; ++k) {
      const std::vector<uint32_t>& v = *data[k];
      const std::vector<uint32_t>& r = *range_ends[k];
      update_counters_avx512(it[k], &v[0] + r[i], cdata, start);
    }

    populate_hits_avx512(counters, cache_size, threshold, start, out);
  }
}

} // namespace fastscancount
#endif
