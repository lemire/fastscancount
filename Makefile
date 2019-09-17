OPT := -O3
CXXFLAGS := -std=c++17 $(OPT) -march=native

counter: benchmark/counters.cpp
	$(CXX) $(CXXFLAGS) $(CXXEXTRA) -o counter benchmark/counters.cpp -Ibenchmark -Iinclude

clean:
	rm -f counter

