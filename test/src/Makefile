CXX      = clang++
CXXFLAGS = -O1 -std=c++17 -DASIO_STANDALONE \
	-Wall -Wno-deprecated-declarations -Wno-unused-private-field \
	-Wno-template-id-cdtor
LDFLAGS = -lpthread -lssl -lcrypto
INCLUDES = -I../../deps/include -I../../include -I../../shared -I../include

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
