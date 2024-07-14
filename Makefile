CXX = g++
CXXFLAGS = -std=c++11 -Wall -pthread

SRCS = proxy_server.cpp proxy_parse.cpp main.cpp
OBJS = $(SRCS:.cpp=.o)
EXEC = proxy_server

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(EXEC) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(EXEC)

.PHONY: all clean
