#include <string>

#include "td-window.hpp"
#include "td-curses-utils.hpp"

PANEL *input_panel = nullptr;
    
void TdWindow::refresh () {
  update_panels ();
  doupdate ();
}

void TdWindow::clear () {
  werase (win);
  box (win, 0, 0);
}

TdWindow::TdWindow(int nlines, int ncols, int begin_y, int begin_x) {
  win = ::newwin (nlines, ncols, begin_y, begin_x);
  wbkgdset (win, 32);
  box (win, 0, 0);

  pan = ::new_panel (win);
  set_panel_userptr (pan, this);
  input_panel = pan;

  update_panels ();
  doupdate ();
}
TdWindow::~TdWindow() {
  if (pan == input_panel) {
    input_panel = panel_below (pan);
  }
  del_panel (pan);
  delwin (win);

  update_panels ();
  doupdate ();
}
int TdWindow::td_addstr (int y, int x, std::string &str, int n) {
  auto wstr = utf8tow (str);
  return td_addstr (y, x, wstr, n);
}
