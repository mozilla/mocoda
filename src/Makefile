CXXFLAGS := -fPIC -O2 -std=c++11
LDFLAGS := -lsqlite3 -L/usr/lib/llvm-4.0/lib -lLLVM
INC := -I/usr/lib/llvm-4.0/include
SRCS = plugin.cpp DB.cpp utils.cpp info.cpp

build: libmocoda.so

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INC) -c $^ -o $@

libmocoda.so: plugin.o DB.o utils.o info.o
	$(CXX) $(LDFLAGS) -shared $^ -o $@

clean:
	$(RM) libmocoda.so *.o

.PHONY: build clean
