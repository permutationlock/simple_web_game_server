CXX      = g++
CXXFLAGS = -O0 -Wall -Wno-unused-result -pthread -lssl -lcrypto
INCLUDES = -I../../include -I../../shared -I../include

TARGET = run_tests
SRCS   = main.cpp minimal_game_test.cpp
OBJS   = $(SRCS:.cpp=.o)
DEPS   = $(SRCS:.cpp=.depends)

.PHONY: clean all

all: $(TARGET)

$(TARGET): $(OBJS)
		$(CXX) $(INCLUDES) $(CXXFLAGS) $(LDFLAGS) $(OBJS) -o $(TARGET)

.cpp.o:
		$(CXX) $(INCLUDES) $(CXXFLAGS) -c $< -o $@

clean:
		rm -f $(OBJS) $(DEPS) $(TARGET)

-include $(DEPS)
