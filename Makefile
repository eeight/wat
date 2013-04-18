all:: wat

wat: wat.cpp
	g++ -std=c++1y $< -o $@ -ggdb3 -lboost_filesystem -lboost_system -lunwind-ptrace -lunwind-generic
