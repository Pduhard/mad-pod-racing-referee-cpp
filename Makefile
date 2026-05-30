# mad-pod-racing-referee-cpp
CXX = g++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra

.PHONY: test
test: src/test.cpp src/engine.h
	$(CXX) $(CXXFLAGS) -o test_engine src/test.cpp -lm
	./test_engine

.PHONY: referee
referee: src/referee.cpp src/engine.h
	$(CXX) $(CXXFLAGS) -o csb-referee src/referee.cpp -lm

.PHONY: clean
clean:
	rm -f test_engine csb-referee referee *_bench
