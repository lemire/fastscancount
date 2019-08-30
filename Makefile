OPT := -O3
CXXFLAGS := -std=c++11 $(OPT) -mavx2

counter: benchmark/counters.cpp
	$(CXX) $(CXXFLAGS) $(CXXEXTRA) -o counter benchmark/counters.cpp -Ibenchmark

clean:
	rm -f counter

