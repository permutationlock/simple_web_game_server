CXX      = g++
CXXFLAGS = -O2 -Wall -Wno-unused-result -pthread
LDFLAGS  =
INCLUDE = -I/home/aven/docs/projects/games/catacrawl/game_server/lib

TARGET = echo_server
SRCS   = echo_server.cpp
OBJS   = $(SRCS:.cpp=.o)
DEPS   = $(SRCS:.cpp=.depends)

.PHONY: clean all

all: $(TARGET)

$(TARGET): $(OBJS)
		$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(INCLUDE) -o $(TARGET)

.cpp.o:
		$(CXX) $(CXXFLAGS) $(INCLUDE) -c $< -o $@

%.depends: %.cpp
		$(CXX) -M $(CXXFLAGS) $(INCLUDE) $< > $@

clean:
		rm -f $(OBJS) $(DEPS) $(TARGET)

-include $(DEPS)
