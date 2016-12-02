#pragma once

#include <ncursesw/ncurses.h>

class RootWindow {
  private:
    WINDOW *win;

  public:
    RootWindow ();

    int td_get_wch (wint_t *ch) {
      return wget_wch (win, ch);
    }

    int lines () const {
      return getmaxy (win);
    }

    int cols () const {
      return getmaxx (win);
    }
};

extern RootWindow *Root;
