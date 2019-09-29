// Fine-grained statistics is available only on Linux
#include "fastscancount.h"
#include "ztimer.h"
#ifdef __AVX2__
#include "fastscancount_avx2.h"
#endif
#include "linux-perf-events-wrapper.h"
#include "maropuparser.h"
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
           float& elapsed,
           std::vector<uint32_t> &answer, size_t sum, size_t expected,
           bool print) {
  WallClockTimer tm;
  unified.start();
  f();
  unified.end();
  elapsed += tm.split();
  if (answer.size() != expected)
    std::cerr << "bug: expected " << expected << " but got " << answer.size()
              << "\n";
#ifdef __linux__
  if (print) {
    double cycles = unified.get_result(PERF_COUNT_HW_CPU_CYCLES);
    double instructions = unified.get_result(PERF_COUNT_HW_INSTRUCTIONS);
    double misses = unified.get_result(PERF_COUNT_HW_BRANCH_MISSES);
    std::cout << name << std::endl;
    std::cout << cycles / sum << " cycles/element " << std::endl;
    std::cout << instructions / cycles << " instructions/cycles " << std::endl;
    std::cout << misses / sum << " miss/element " << std::endl;
  }
#endif
}

void demo_data(const std::vector<std::vector<uint32_t>>& data,
              const std::vector<std::vector<uint32_t>>& queries,
              size_t threshold) {
  size_t N = 0;
  for (const auto& data_elem : data) {
    size_t sz = data_elem.size();
    if (sz) {
      N = std::max(N, (size_t)data_elem[sz-1] + 1);
    }
  }

  std::vector<uint8_t> counters(N);
  std::vector<uint32_t> answer;
  answer.reserve(N);

  std::vector<int> evts = {
#ifdef __linux__
                           PERF_COUNT_HW_CPU_CYCLES,
                           PERF_COUNT_HW_INSTRUCTIONS,
                           PERF_COUNT_HW_BRANCH_MISSES,
                           PERF_COUNT_HW_CACHE_REFERENCES,
                           PERF_COUNT_HW_CACHE_MISSES
#endif
                          };
  LinuxEventsWrapper unified(evts);

  std::vector<const std::vector<uint32_t>*> dataPtrs;

  float elapsed = 0, elapsed_fast = 0, elapsed_avx = 0;

  size_t sum_total = 0;

  for (size_t qid = 0; qid < queries.size(); ++qid) {
    const auto& query_elem = queries[qid];
    dataPtrs.clear();
    size_t sum = 0;
    for (uint32_t idx : query_elem) {
      if (idx >= data.size()) {
        std::stringstream err;
        err << "Inconsistent data, posting " << idx << 
               " is >= # of postings " << data.size() << " query id " << qid;
        throw std::runtime_error(err.str());
      }
      sum += data[idx].size();
      dataPtrs.push_back(&data[idx]);
    }
    sum_total += sum;

    scancount(counters, dataPtrs, answer, threshold);
    const size_t expected = answer.size();
    std::cout << "Qid: " << qid << " got " << expected << " hits\n";

    bool last = (qid == queries.size() - 1);

    bench(
        [&]() {
          scancount(counters, dataPtrs, answer, threshold);
        },
        "optimized cache-sensitive scancount", unified, elapsed, answer, sum,
        expected, last);

    bench(
        [&]() {
          fastscancount::fastscancount(dataPtrs, answer, threshold);
        },
        "optimized cache-sensitive scancount", unified, elapsed_fast, answer, sum,
        expected, last);
  
#ifdef __AVX2__
    bench(
        [&]() {
          fastscancount::fastscancount_avx2(dataPtrs, answer, threshold);
        },
        "AVX2-based scancount", unified, elapsed_avx, answer, sum, expected, last);
#endif
  }
  std::cout << "Elems per millisecond:" << std::endl;
  std::cout << "scancount: " << (sum_total/(elapsed/1e3)) << std::endl; 
  std::cout << "fastscancount: " << (sum_total/(elapsed_fast/1e3)) << std::endl; 
#ifdef __AVX2__
  std::cout << "fastscancount_avx2: " << (sum_total/(elapsed_avx/1e3)) << std::endl; 
#endif
}

void demo_random(size_t N, size_t length, size_t array_count, size_t threshold) {
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
  std::vector<int> evts = {
#ifdef __linux__
                           PERF_COUNT_HW_CPU_CYCLES,
                           PERF_COUNT_HW_INSTRUCTIONS,
                           PERF_COUNT_HW_BRANCH_MISSES,
                           PERF_COUNT_HW_CACHE_REFERENCES,
                           PERF_COUNT_HW_CACHE_MISSES
#endif
                          };
  LinuxEventsWrapper unified(evts);
  float elapsed = 0, elapsed_fast = 0, elapsed_avx = 0;
  scancount(counters, dataPtrs, answer, threshold);
  const size_t expected = answer.size();
  std::cout << "Got " << expected << " hits\n";
  size_t sum_total = 0;
  for (size_t t = 0; t < REPEATS; t++) {

    sum_total += sum;

    bool last = (t == REPEATS - 1);

    bench(
        [&]() {
          scancount(counters, dataPtrs, answer, threshold);
        },
        "baseline scancount", unified, elapsed, answer, sum,
        expected, last);

    bench(
        [&]() {
          fastscancount::fastscancount(dataPtrs, answer, threshold);
        },
        "optimized cache-sensitive scancount", unified, elapsed_fast, answer, sum,
        expected, last);

#ifdef __AVX2__
    bench(
        [&]() {
          fastscancount::fastscancount_avx2(dataPtrs, answer, threshold);
        },
        "AVX2-based scancount", unified, elapsed_avx, answer, sum, expected, last);
#endif
  }
  std::cout << "Elems per millisecond:" << std::endl;
  std::cout << "scancount: " << (sum_total/(elapsed/1e3)) << std::endl; 
  std::cout << "fastscancount: " << (sum_total/(elapsed_fast/1e3)) << std::endl; 
#ifdef __AVX2__
  std::cout << "fastscancount_avx2: " << (sum_total/(elapsed_avx/1e3)) << std::endl; 
#endif
}

void usage(const std::string& err="") {
  if (!err.empty()) {
    std::cerr << err << std::endl;
  }
  std::cerr << "usage: --postings <postings file> --queries <queries file> --threshold <threshold>" << std::endl;
}

int main(int argc, char *argv[]) {
  // A very naive way to process arguments, 
  // but it's ok unless we need to extend it substantially.
  if (argc != 1) {
    if (argc != 7) {
      usage("");
      return EXIT_FAILURE; 
    }
    std::string postings_file, queries_file;
    int threshold = -1;
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--postings") {
        postings_file = argv[++i];
      } else if (std::string(argv[i]) == "--queries") {
        queries_file = argv[++i];
      } else if (std::string(argv[i]) == "--threshold") {
        threshold = std::atoi(argv[++i]);
      }
    }
    if (postings_file.empty() || queries_file.empty() || threshold < 0) {
      usage("Specify queries, postings, and the threshold!");
      return EXIT_FAILURE; 
    }
    std::vector<uint32_t> tmp; 
    std::vector<std::vector<uint32_t>> data;
    {
      MaropuGapReader drdr(postings_file);
      if (!drdr.open()) {
        usage("Cannot open: " + postings_file);
        return EXIT_FAILURE; 
      }
      while (drdr.loadIntegers(tmp)) {
        data.push_back(tmp);
      }
    }
    std::vector<std::vector<uint32_t>> queries;
    {
      MaropuGapReader qrdr(queries_file);
      if (!qrdr.open()) {
        usage("Cannot open: " + queries_file);
        return EXIT_FAILURE; 
      }
      while (qrdr.loadIntegers(tmp)) {
        queries.push_back(tmp);
      }
    }
              
    demo_data(data, queries, threshold);
  } else {
    demo_random(20000000, 50000, 100, 3);
  }
  return EXIT_SUCCESS;
}
