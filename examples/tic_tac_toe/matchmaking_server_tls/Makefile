CXX      = g++
CXXFLAGS = -O1 -Wall -std=c++17 -pthread -lboost_log -lssl -lcrypto
INCLUDES = -I../../../include -I../../../shared

TARGET = matchmaking_server
SRCS   = matchmaking_server.cpp
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
