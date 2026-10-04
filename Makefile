# ============================================================
#  Makefile — Network Latency & Packet Loss Chaos Emulator
#  Works on Ubuntu / WSL with g++ and C++17
# ============================================================

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
TARGET   = network_detector
SRCS     = main.cpp emulator.cpp
OBJS     = $(SRCS:.cpp=.o)

# Default target
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp emulator.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	sudo ./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean run
