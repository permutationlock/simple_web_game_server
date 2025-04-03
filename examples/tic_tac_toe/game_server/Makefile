CXX      = clang++
CXXFLAGS = -O1 -std=c++17 -DASIO_STANDALONE \
	-Wall -Wno-deprecated-declarations -Wno-unused-private-field
LDFLAGS = -lpthread -lssl -lcrypto
INCLUDES = -I../../../deps/include -I../../../include -I../../../shared

TARGET = game_server
SRCS   = game_server.cpp
OBJS   = $(SRCS:.cpp=.o)
DEPS   = $(SRCS:.cpp=.depends)

.PHONY: clean all

all: $(TARGET)

$(TARGET): $(OBJS)
		$(CXX) $(INCLUDES) $(LDFLAGS) $(OBJS) -o $(TARGET)

.cpp.o:
		$(CXX) $(INCLUDES) $(CXXFLAGS) -c $< -o $@

clean:
		rm -f $(OBJS) $(DEPS) $(TARGET)

-include $(DEPS)
