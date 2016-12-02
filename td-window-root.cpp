#include "td-window-root.hpp"

RootWindow::RootWindow () {
  win = initscr ();
  set_escdelay (25);
  noecho ();
  cbreak ();
  nodelay (win, 1);
  keypad (win, 1);
}

RootWindow *Root;
