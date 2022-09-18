
CXXFLAGS=-Isrc
ifeq ($(DEBUG),1)
CXXFLAGS+=-g -Og
else
CXXFLAGS+=-O3
endif

all: test/test

test/test: test/test.cpp src/dircache.cpp 
	$(CXX) $(CXXFLAGS) -o test/test src/dircache.cpp test/test.cpp -lpthread
	
clean: 
	rm test/test || true
