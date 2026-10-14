CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
BUILD    = build

LIB_SRCS = memory_system.cpp single_level_pt.cpp two_level_pt.cpp tlb.cpp \
           bump_frame_manager.cpp lru_frame_manager.cpp trace_reader.cpp benchmark.cpp report_table.cpp
LIB_OBJS = $(LIB_SRCS:%.cpp=$(BUILD)/%.o)
PARTS    = part_i part_ii part_iii part_iv
PROGRAMS = $(PARTS:%=$(BUILD)/%)

all: $(PROGRAMS)

$(PROGRAMS): $(BUILD)/%: $(BUILD)/%.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Every object depends on every header, so editing a header rebuilds everything.
$(BUILD)/%.o: %.cpp $(wildcard *.hpp) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

# Runs every part on the full traces; each part prints its table and saves results/<part>.csv.
results: all
	for p in $(PARTS); do ./$(BUILD)/$$p; done

clean:
	rm -rf $(BUILD)

.PHONY: all results clean
