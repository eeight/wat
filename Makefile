CXXFLAGS := -std=c++1y -ggdb3 -Wall -Wextra -Werror
LIBS := boost_filesystem boost_system unwind-ptrace unwind-generic ncurses
all:: wat

%.o: %.cpp
	g++ -c $(CXXFLAGS) $*.cpp -o $@ -MD -MF $*.d

wat: $(patsubst %.cpp,%.o,$(wildcard *.cpp))
	g++ $(CXXFLAGS) $^ -o $@ $(addprefix -l,$(LIBS))

-include *.d

clean::
	$(RM) *.o
	$(RM) *.d
	$(RM) wat
