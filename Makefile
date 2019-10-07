OPT := -O3
# Leo really doubts -mavx2 helps anything, but one can
# disable avx512 tests by enforcing -mavx2
#CXXFLAGS := -std=c++17 $(OPT) -mavx2
CXXFLAGS := -std=c++17 $(OPT) -march=native

counter: benchmark/counters.cpp include/*.h Makefile
	$(CXX) $(CXXFLAGS) $(CXXEXTRA) -o counter benchmark/counters.cpp -Ibenchmark -Iinclude

clean:
	rm -f counter

