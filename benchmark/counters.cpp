// we require Linux
#include "fastscancount.h"
#ifdef __AVX2__
#include "fastscancount_avx2.h"
#endif
#include "linux-perf-events-wrapper.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <immintrin.h>
#include <iostream>
#include <vector>

#define REPEATS 10
void scancount(std::vector<uint8_t> &counters,
               std::vector<const std::vector<uint32_t>*> &data,
               std::vector<uint32_t> &out, size_t threshold) {
  std::fill(counters.begin(), counters.end(), 0);
  out.clear();
  for (size_t c = 0; c < data.size(); c++) {
    const std::vector<uint32_t> &v = *data[c];
    for (size_t i = 0; i < v.size(); i++) {
      counters[v[i]]++;
    }
  }
  for (uint32_t i = 0; i < counters.size(); i++) {
    if (counters[i] > threshold)
      out.push_back(i);
  }
}

template <typename F>
void bench(F f, const std::string &name,
           LinuxEventsWrapper &unified,
           std::vector<uint32_t> &answer, size_t sum, size_t expected,
           bool print) {
  unified.start();
  f(answer);
  unified.end();
  if (answer.size() != expected)
    std::cerr << "bug: expected " << expected << " but got " << answer.size()
              << "\n";
  if (print) {
    double cycles = unified.get_result(PERF_COUNT_HW_CPU_CYCLES);
    double instructions = unified.get_result(PERF_COUNT_HW_INSTRUCTIONS);
    double misses = unified.get_result(PERF_COUNT_HW_BRANCH_MISSES);
    std::cout << name << std::endl;
    std::cout << cycles / sum << " cycles/element " << std::endl;
    std::cout << instructions / cycles << " instructions/cycles " << std::endl;
    std::cout << misses / sum << " miss/element " << std::endl;
  }
}

void demo(size_t N, size_t length, size_t array_count, size_t threshold) {
  std::vector<std::vector<uint32_t>> data(array_count);
  std::vector<const std::vector<uint32_t>*> dataPtrs;
  std::vector<uint8_t> counters(N);
  std::vector<uint32_t> answer;
  answer.reserve(N);

  size_t sum = 0;
  for (size_t c = 0; c < array_count; c++) {
    std::vector<uint32_t> &v = data[c];
    for (size_t i = 0; i < length; i++) {
      v.push_back(rand() % N);
    }
    std::sort(v.begin(), v.end());
    v.resize(std::distance(v.begin(), unique(v.begin(), v.end())));
    sum += v.size();
    dataPtrs.push_back(&data[c]);
  }
  std::vector<int> evts = {PERF_COUNT_HW_CPU_CYCLES,
                           PERF_COUNT_HW_INSTRUCTIONS,
                           PERF_COUNT_HW_BRANCH_MISSES,
                           PERF_COUNT_HW_CACHE_REFERENCES,
                           PERF_COUNT_HW_CACHE_MISSES};
  LinuxEventsWrapper unified(evts);
  scancount(counters, dataPtrs, answer, threshold);
  const size_t expected = answer.size();
  std::cout << "Got " << expected << " hits\n";
  for (size_t t = 0; t < REPEATS; t++) {

    bool last = (t == REPEATS - 1);

    bench(
        [&](std::vector<uint32_t> &ans) {
          fastscancount::fastscancount(dataPtrs, answer, threshold);
        },
        "optimized cache-sensitive scancount", unified, answer, sum,
        expected, last);

#ifdef __AVX2__
    bench(
        [&](std::vector<uint32_t> &ans) {
          fastscancount::fastscancount_avx2(counters, dataPtrs, answer, threshold);
        },
        "AVX2-based scancount", unified, answer, sum, expected, last);
#endif
  }
}
int main() {
  demo(20000000, 50000, 100, 3);
  return EXIT_SUCCESS;
}
