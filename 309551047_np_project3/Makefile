.PHONY: clean http_server console cgi_server part1 part2 misc

CXX=g++
CXXFLAGS=-std=c++14 -pthread -lboost_system
CXX_INCLUDE_DIRS=/usr/local/include
CXX_INCLUDE_PARAMS=$(addprefix -I , $(CXX_INCLUDE_DIRS))
CXX_LIB_DIRS=/usr/local/lib
CXX_LIB_PARAMS=$(addprefix -L , $(CXX_LIB_DIRS))

WIN_CXXFLAGS=-std=c++14 -lws2_32 -lwsock32
WIN_CXX_INCLUDE_DIRS=C:\\MinGW\\include
WIN_CXX_INCLUDE_PARAMS=$(addprefix -I , $(WIN_CXX_INCLUDE_DIRS))
WIN_CXX_LIB_DIRS=C:\\MinGW\\lib
WIN_CXX_LIB_PARAMS=$(addprefix -L , $(WIN_CXX_LIB_DIRS))

all: part1 part2

part1: http_server console
part2: cgi_server
part2-dbg: cgi_server-dbg

http_server: http_server.cpp
	$(CXX) http_server.cpp -o http_server $(CXX_INCLUDE_PARAMS) $(CXX_LIB_PARAMS) $(CXXFLAGS)

console: console.cpp
	$(CXX) console.cpp -o console.cgi $(CXX_INCLUDE_PARAMS) $(CXX_LIB_PARAMS) $(CXXFLAGS)

cgi_server: cgi_server.cpp
	$(CXX) cgi_server.cpp -o cgi_server $(WIN_CXXFLAGS)

cgi_server-dbg: cgi_server.cpp
	$(CXX) cgi_server.cpp -o cgi_server $(WIN_CXX_INCLUDE_PARAMS) $(WIN_CXX_LIB_PARAMS) $(WIN_CXXFLAGS)

misc: 
	mkdir -p bin
	$(CXX) noop.cpp -o ./bin/noop
	$(CXX) number.cpp -o ./bin/number
	$(CXX) removetag.cpp -o ./bin/removetag
	$(CXX) removetag0.cpp -o ./bin/removetag0
	$(CXX) delayedremovetag.cpp -o ./bin/delayedremovetag


clean:
	rm -f http_server
	rm -f console.cgi
	rm -f cgi_server.exe

	