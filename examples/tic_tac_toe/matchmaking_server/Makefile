CXX      = clang++
CXXFLAGS = -O1 -std=c++17 -DASIO_STANDALONE \
	-Wall -Wno-deprecated-declarations -Wno-unused-private-field \
	-Wno-template-id-cdtor
LDFLAGS = -lpthread -lssl -lcrypto
INCLUDES = -I../../../deps/include -I../../../include -I../../../shared

TARGET = matchmaking_server
SRCS   = matchmaking_server.cpp
OBJS   = $(SRCS:.cpp=.o)
DEPS   = $(SRCS:.cpp=.depends)

.PHONY: clean all run

all: $(TARGET) public_html

public_html:
		cd client; npm install; npm run build; cp -r build ../public_html

$(TARGET): $(OBJS)
		$(CXX) $(INCLUDES) $(LDFLAGS) $(OBJS) -o $(TARGET)

.cpp.o:
		$(CXX) $(INCLUDES) $(CXXFLAGS) -c $< -o $@

clean:
		rm -f $(OBJS) $(DEPS) $(TARGET)
		rm -rf public_html

-include $(DEPS)
