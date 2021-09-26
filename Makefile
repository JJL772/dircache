
all: 
	g++ -g -o test $(wildcard *.cpp)
	
clean: 
	rm test