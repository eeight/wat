#include "text_table.h"

#include <curses.h>

class TextTable {
public:
    TextTable() {
        initscr();
    }

    ~TextTable() {
        //endwin();
    }
};

void putLines(const std::vector<std::string>& lines) {
    static TextTable textTable;

    erase();
    move(0, 0);
    int y = 0;
    for (const auto& line: lines) {
        mvaddstr(y++, 0, line.c_str());
    }
    refresh();
}
