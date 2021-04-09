all: clean np_simple np_single_proc

np_simple: 
	g++ -std=c++11 np_simple.cpp -o np_simple 

np_single_proc: 
	g++ -std=c++11 np_single_proc.cpp -o np_single_proc -g

clean:
	rm -f np_simple
	rm -f np_single_proc