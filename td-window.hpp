#pragma once

#include "ncursesw/ncurses.h"
#include "ncursesw/panel.h"

#include "termkey.h"

extern PANEL *input_panel;

class TdWindow {
  private:
    WINDOW *win;
    PANEL *pan;
  public:
    TdWindow(int nlines, int ncols, int begin_y, int begin_x);

    virtual void key_pressed (const TermKeyKey *key) {
    }

    virtual void fn_key_pressed (const TermKeyKey *key) {
    }

    virtual void sym_key_pressed (const TermKeyKey *key) {
    }

    virtual void redraw () {
    }

    void refresh ();
    void clear ();

    int td_addstr (int y, int x, std::wstring &wstr, int n = -1) {
      return (::wmove (win, y, x) == ERR ? ERR : ::waddnwstr (win, wstr.c_str (), n));
    }
    
    int td_addstr (int y, int x, std::string &str, int n = -1);

    int td_move (int y, int x) {
      return ::wmove (win, y, x);
    }

    int lines () const {
      return getmaxy (win);
    }

    int cols () const {
      return getmaxx (win);
    }
    
    virtual ~TdWindow();    
};

