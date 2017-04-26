CXXFLAGS := -fPIC -O2 -std=c++11 -fno-rtti
LDFLAGS ?= -lsqlite3
INC ?= -I/usr/lib/llvm-4.0/include
SRCS = plugin.cpp DB.cpp utils.cpp info.cpp
CXX=g++

build: libmocoda.so

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INC) -c $^ -o $@

libmocoda.so: plugin.o DB.o utils.o info.o
	$(CXX) $(LDFLAGS) -shared $^ -o $@

clean:
	$(RM) libmocoda.so *.o

.PHONY: build clean
