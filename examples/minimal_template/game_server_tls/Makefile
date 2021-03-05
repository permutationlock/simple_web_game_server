CXX      = g++
CXXFLAGS = -O1 -Wall -std=c++17 -pthread -lssl -lcrypto -ltbb
INCLUDES = -I../../../include -I../../../shared

TARGET = game_server
SRCS   = game_server.cpp
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
