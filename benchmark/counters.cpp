// we require Linux
#include "fastscancount.h"
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

  std::vector<int> evts = {PERF_COUNT_HW_CPU_CYCLES,
                           PERF_COUNT_HW_INSTRUCTIONS,
                           PERF_COUNT_HW_BRANCH_MISSES,
                           PERF_COUNT_HW_CACHE_REFERENCES,
                           PERF_COUNT_HW_CACHE_MISSES};
  LinuxEventsWrapper unified(evts);

  std::vector<const std::vector<uint32_t>*> dataPtrs;

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

    scancount(counters, dataPtrs, answer, threshold);
    const size_t expected = answer.size();
#define RUNNINGTESTS
#ifdef RUNNINGTESTS
    fastscancount::fastscancount(dataPtrs, answer, threshold);
    size_t s1 = answer.size();
    auto a1 (answer);
    std::sort(a1.begin(), a1.end());
    fastscancount::fastscancount_avx2(dataPtrs, answer, threshold);
    size_t s2 = answer.size();
    auto a2 (answer);
    std::sort(a2.begin(), a2.end());
    if (a1 != a2) {
      std::cout << "s1: " << s1 << " s2: " << s2 << std::endl;
      for(size_t j = 0; j < s1; j++) {
        std::cout << j << " " << a1[j] << " vs " << a2[j] ;

        if(a1[j] != a2[j]) std::cout << " oh oh ";
        std::cout << std::endl;
      }
      throw new std::runtime_error("bug");
    }
#endif 
    std::cout << "Qid: " << qid << " got " << expected << " hits\n";
    bool last = (qid == queries.size() - 1);

    bench(
        [&](std::vector<uint32_t> &ans) {
          fastscancount::fastscancount(dataPtrs, answer, threshold);
        },
        "optimized cache-sensitive scancount", unified, answer, sum,
        expected, last);
  
#ifdef __AVX2__
    bench(
        [&](std::vector<uint32_t> &ans) {
          fastscancount::fastscancount_avx2(dataPtrs, answer, threshold);
        },
        "AVX2-based scancount", unified, answer, sum, expected, last);
#endif
  }
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
          fastscancount::fastscancount_avx2(dataPtrs, answer, threshold);
        },
        "AVX2-based scancount", unified, answer, sum, expected, last);
#endif
  }
}

void usage(const std::string& err="") {
  if (!err.empty()) {
    std::cerr << "Specify both the queries and the postings!" << std::endl;
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
