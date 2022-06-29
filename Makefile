
all: 
	g++ -g -o test $(wildcard *.cpp) -lpthread
	
clean: 
	rm test