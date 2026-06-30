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

# Replay every recorded CG game in validate/ through the engine and diff each
# turn's pod state against the recording (byte-parity check).
.PHONY: validate
validate: validate/replay_test.cpp src/engine.h
	$(CXX) $(CXXFLAGS) -o validate/replay_test validate/replay_test.cpp -lm
	@for fx in validate/fixture_*.txt; do \
		echo "== $$fx =="; ./validate/replay_test "$$fx"; \
	done

.PHONY: clean
clean:
	rm -f test_engine csb-referee referee *_bench validate/replay_test
