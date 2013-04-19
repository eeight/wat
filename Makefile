CXXFLAGS := -std=c++1y -ggdb3
LIBS := boost_filesystem boost_system unwind-ptrace unwind-generic
all:: wat

wat.o: wat.cpp symbols.h scope.h
symbols.o: symbols.cpp symbols.h scope.h

%.o: %.cpp
	g++ -c $(CXXFLAGS) $*.cpp -o $@

wat: wat.o symbols.o
	g++ $(CXXFLAGS) $^ -o $@ $(addprefix -l,$(LIBS))
