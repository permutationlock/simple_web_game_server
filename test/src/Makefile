CXX      = g++
CXXFLAGS = -O0 -Wall -std=c++14 -pthread -lssl -lcrypto -ltbb
INCLUDES = -I../../include -I../../shared -I../include

TARGET = run_tests
SRCS   = main.cpp client_test.cpp test_game_test.cpp game_server_test.cpp matchmaking_server_test.cpp
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
