CXXFLAGS := -std=c++1y -ggdb3
LIBS := boost_filesystem boost_system unwind-ptrace unwind-generic ncurses
all:: wat

wat.o: wat.cpp symbols.h scope.h text_table.h
symbols.o: symbols.cpp symbols.h scope.h
text_table.o: text_table.cpp text_table.h

%.o: %.cpp
	g++ -c $(CXXFLAGS) $*.cpp -o $@

wat: wat.o symbols.o text_table.o
	g++ $(CXXFLAGS) $^ -o $@ $(addprefix -l,$(LIBS))
