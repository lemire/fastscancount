// we require Linux
#include "fastscancount.h"
#include "fastscancount_avx2.h"
#include "linux-perf-events.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <immintrin.h>
#include <iostream>
#include <vector>

#define REPEATS 10
void scancount(std::vector<uint8_t> &counters,
               std::vector<std::vector<uint32_t>> &data,
               std::vector<uint32_t> &out, size_t threshold) {
  std::fill(counters.begin(), counters.end(), 0);
  out.clear();
  for (size_t c = 0; c < data.size(); c++) {
    std::vector<uint32_t> &v = data[c];
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
           LinuxEvents<PERF_TYPE_HARDWARE> &unified,
           std::vector<unsigned long long> &results,
           std::vector<uint32_t> &answer, size_t sum, size_t expected,
           bool print) {
  unified.start();
  f(answer);
  unified.end(results);
  if (answer.size() != expected)
    std::cerr << "bug: expected " << expected << " but got " << answer.size()
              << "\n";
  if (print) {
    std::cout << name << std::endl;
    std::cout << double(results[0]) / sum << " cycles/element " << std::endl;
    std::cout << double(results[1]) / double(results[0]) << " instructions/cycles " << std::endl;
    std::cout << double(results[2]) / sum << " miss/element " << std::endl;
  }
}

void demo(size_t N, size_t length, size_t array_count, size_t threshold) {
  std::vector<std::vector<uint32_t>> data(array_count);
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
  }
  std::vector<int> evts;
  evts.push_back(PERF_COUNT_HW_CPU_CYCLES);
  evts.push_back(PERF_COUNT_HW_INSTRUCTIONS);
  evts.push_back(PERF_COUNT_HW_BRANCH_MISSES);
  evts.push_back(PERF_COUNT_HW_CACHE_REFERENCES);
  evts.push_back(PERF_COUNT_HW_CACHE_MISSES);
  LinuxEvents<PERF_TYPE_HARDWARE> unified(evts);
  std::vector<unsigned long long> results;
  results.resize(evts.size());
  scancount(counters, data, answer, threshold);
  const size_t expected = answer.size();
  std::cout << "Got " << expected << " hits\n";
  for (size_t t = 0; t < REPEATS; t++) {

    bool last = (t == REPEATS - 1);

    bench(
        [&](std::vector<uint32_t> &ans) {
          fastscancount::fastscancount(data, answer, threshold);
        },
        "optimized cache-sensitive scancount", unified, results, answer, sum,
        expected, last);

    bench(
        [&](std::vector<uint32_t> &ans) {
          fastscancount::fastscancount_avx2(counters, data, answer, threshold);
        },
        "AVX2-based scancount", unified, results, answer, sum, expected, last);
  }
}
int main() {
  demo(20000000, 50000, 100, 3);
  return EXIT_SUCCESS;
}
