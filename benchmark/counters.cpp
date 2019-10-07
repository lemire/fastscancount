// Fine-grained statistics is available only on Linux
#include "fastscancount.h"
#include "ztimer.h"
#ifdef __AVX2__
#include "fastscancount_avx2.h"
#endif
#ifdef __AVX512F__
#include "fastscancount_avx512.h"
#endif
#include "linux-perf-events-wrapper.h"
#include "maropuparser.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <immintrin.h>
#include <iostream>
#include <vector>
#include <stdexcept>

#define REPEATS 10
#define RUNNINGTESTS

void scancount(const std::vector<const std::vector<uint32_t>*> &data,
               std::vector<uint32_t> &out, size_t threshold) {
  uint64_t largest = 0;
  for(auto z : data) {
    const std::vector<uint32_t> & v = *z;
    if(v[v.size() - 1] > largest) largest = v[v.size() - 1];
  }
  std::vector<uint8_t> counters(largest+1);
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

void calc_boundaries(uint32_t largest, uint32_t range_size, 
                    const std::vector<uint32_t>& data, 
                    std::vector<uint32_t>& range_ends) {
  if (!range_size) {
    throw std::runtime_error("range_size must be > 0");
  }
  uint32_t end = 0;
  range_ends.clear();
  
  for (uint32_t start = 0; start <= largest; start += range_size) {
    uint32_t curr_max = std::min(largest, start + range_size - 1);
    while (end < data.size() && data[end] <= curr_max) {
      end++;
    }
    range_ends.push_back(end);
  }
} 

const uint32_t range_size_avx512 = 40000;

void calc_alldata_boundaries(const std::vector<std::vector<uint32_t>>& data,
                             std::vector<std::vector<uint32_t>>& range_ends,
                             size_t range_size) {
  uint32_t largest = 0;
  range_ends.clear();
  range_ends.resize(data.size());
  for(const auto& v : data) {
    if (!v.empty() && v[v.size() - 1] > largest) largest = v[v.size() - 1];
  }
  for (unsigned i = 0; i < data.size(); ++i) {
    calc_boundaries(largest, range_size, data[i], range_ends[i]); 
  }
}

template <typename F>
void test(F f, const std::vector<const std::vector<uint32_t>*>& data_ptrs,
          std::vector<uint32_t>& answer, unsigned threshold, const std::string &name) {
  scancount(data_ptrs, answer, threshold);
  size_t s1 = answer.size();
  auto a1 (answer);
  std::sort(a1.begin(), a1.end());
  answer.clear();
  f();
  size_t s2 = answer.size();
  auto a2 (answer);
  std::sort(a2.begin(), a2.end());
  if (a1 != a2) {
    std::cout << "s1: " << s1 << " s2: " << s2 << std::endl;
    for(size_t j = 0; j < std::min(s1, s2); j++) {
      std::cout << j << " " << a1[j] << " vs " << a2[j] ;

      if(a1[j] != a2[j]) std::cout << " oh oh ";
      std::cout << std::endl;
    }
    throw std::runtime_error("bug: " + name);
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

  std::vector<std::vector<uint32_t>> range_boundaries;
  calc_alldata_boundaries(data, range_boundaries, range_size_avx512);

  std::vector<const std::vector<uint32_t>*> data_ptrs;
  std::vector<const std::vector<uint32_t>*> range_ptrs;

  float elapsed = 0, elapsed_fast = 0, elapsed_avx = 0, elapsed_avx512 = 0;

  size_t sum_total = 0;

  for (size_t qid = 0; qid < queries.size(); ++qid) {
    const auto& query_elem = queries[qid];
    data_ptrs.clear();
    range_ptrs.clear();
    size_t sum = 0;
    for (uint32_t idx : query_elem) {
      if (idx >= data.size()) {
        std::stringstream err;
        err << "Inconsistent data, posting " << idx << 
               " is >= # of postings " << data.size() << " query id " << qid;
        throw std::runtime_error(err.str());
      }
      sum += data[idx].size();
      data_ptrs.push_back(&data[idx]);
      range_ptrs.push_back(&range_boundaries[idx]);
    }
    sum_total += sum;

    scancount(data_ptrs, answer, threshold);
    const size_t expected = answer.size();

#ifdef RUNNINGTESTS
    test(
      [&](){
        fastscancount::fastscancount(data_ptrs, answer, threshold);
      }, data_ptrs, answer, threshold, "fastscancount"
    );
#ifdef __AVX2__
    test(
      [&](){
        fastscancount::fastscancount_avx2(data_ptrs, answer, threshold);
      }, data_ptrs, answer, threshold, "fastscancount_avx2"
    );
#endif

#ifdef __AVX512F__
    test(
      [&](){
        fastscancount::fastscancount_avx512(range_size_avx512, data_ptrs, range_ptrs, answer, threshold);
      }, data_ptrs, answer, threshold, "fastscancount_avx512"
    );
#endif

#endif
    std::cout << "Qid: " << qid << " got " << expected << " hits\n";

    bool last = (qid == queries.size() - 1);

    bench(
        [&]() {
          scancount(data_ptrs, answer, threshold);
        },
        "baseline scancount", unified, elapsed, answer, sum,
        expected, last);

    bench(
        [&]() {
          fastscancount::fastscancount(data_ptrs, answer, threshold);
        },
        "optimized cache-sensitive scancount", unified, elapsed_fast, answer, sum,
        expected, last);
#ifdef __AVX512F__
    bench(
        [&]() {
          fastscancount::fastscancount_avx512(range_size_avx512, data_ptrs, range_ptrs, answer, threshold);
        },
        "AVX512-based scancount", unified, elapsed_avx512, answer, sum, expected, last);
#endif
#ifdef __AVX2__
    bench(
        [&]() {
          fastscancount::fastscancount_avx2(data_ptrs, answer, threshold);
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
#ifdef __AVX512F__
  std::cout << "fastscancount_avx512: " << (sum_total/(elapsed_avx512/1e3)) << std::endl; 
#endif

}


void demo_random(size_t N, size_t length, size_t array_count, size_t threshold) {
  std::vector<std::vector<uint32_t>> data(array_count);

  std::vector<const std::vector<uint32_t>*> data_ptrs;
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
    data_ptrs.push_back(&data[c]);
  }


  std::vector<std::vector<uint32_t>> range_boundaries;
  calc_alldata_boundaries(data, range_boundaries, range_size_avx512);
  std::vector<const std::vector<uint32_t>*> range_ptrs;
  for (size_t c = 0; c < array_count; c++) {
    range_ptrs.push_back(&range_boundaries[c]);
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
  float elapsed = 0, elapsed_fast = 0, elapsed_avx = 0, elapsed_avx512 = 0;
  scancount(data_ptrs, answer, threshold);
  const size_t expected = answer.size();
  std::cout << "Got " << expected << " hits\n";
  size_t sum_total = sum * REPEATS;
  for (size_t t = 0; t < REPEATS; t++) {
    bool last = (t == REPEATS - 1);

    bench(
        [&]() {
          scancount(data_ptrs, answer, threshold);
        },
        "baseline scancount", unified, elapsed, answer, sum,
        expected, last);
  }

  for (size_t t = 0; t < REPEATS; t++) {
    bool last = (t == REPEATS - 1);

#ifdef RUNNINGTESTS
    test(
      [&](){
        fastscancount::fastscancount(data_ptrs, answer, threshold);
      }, data_ptrs, answer, threshold, "fastscancount"
    );
#endif

    bench(
        [&]() {
          fastscancount::fastscancount(data_ptrs, answer, threshold);
        },
        "optimized cache-sensitive scancount", unified, elapsed_fast, answer, sum,
        expected, last);
  }

  for (size_t t = 0; t < REPEATS; t++) {
    bool last = (t == REPEATS - 1);

#ifdef __AVX2__
#ifdef RUNNINGTESTS
    test(
      [&](){
        fastscancount::fastscancount_avx2(data_ptrs, answer, threshold);
      }, data_ptrs, answer, threshold, "fastscancount_avx2"
    );
#endif
    bench(
        [&]() {
          fastscancount::fastscancount_avx2(data_ptrs, answer, threshold);
        },
        "AVX2-based scancount", unified, elapsed_avx, answer, sum, expected, last);
#endif
  }

  for (size_t t = 0; t < REPEATS; t++) {
    bool last = (t == REPEATS - 1);
#ifdef __AVX512F__
#ifdef RUNNINGTESTS
    test(
      [&](){
        fastscancount::fastscancount_avx512(range_size_avx512, data_ptrs, range_ptrs, answer, threshold);
      }, data_ptrs, answer, threshold, "fastscancount_avx512"
    );
#endif

    bench(
        [&]() {
          fastscancount::fastscancount_avx512(range_size_avx512, data_ptrs, range_ptrs, answer, threshold);
        },
        "AVX512-based scancount", unified, elapsed_avx512, answer, sum, expected, last);
#endif
  }

  std::cout << "Elems per millisecond:" << std::endl;
  std::cout << "scancount: " << (sum_total/(elapsed/1e3)) << std::endl; 
  std::cout << "fastscancount: " << (sum_total/(elapsed_fast/1e3)) << std::endl; 
#ifdef __AVX2__
  std::cout << "fastscancount_avx2: " << (sum_total/(elapsed_avx/1e3)) << std::endl; 
#endif
#ifdef __AVX512F__
  std::cout << "fastscancount_avx512: " << (sum_total/(elapsed_avx512/1e3)) << std::endl; 
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
              
    try { 
      demo_data(data, queries, threshold);
    } catch (const std::exception& e) {
      std::cerr << "Exception: " << e.what() << std::endl;
      return EXIT_FAILURE;
    }
  } else {
    try {
      // Previous demo with threshold 3
      //demo_random(20000000, 50000, 100, 3);
      for (unsigned k = 1; k < 10; ++k) {
        std::cout << "Demo threshold:" << k << std::endl;
        demo_random(20000000, 50000, 100, k);
        std::cout << "=======================" << std::endl;
      }
    } catch (const std::exception& e) {
      std::cerr << "Exception: " << e.what() << std::endl;
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}
