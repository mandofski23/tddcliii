#include "td-window-log.hpp"
#include "td-curses-utils.hpp"
    
void TdLogWindow::redraw () {
  clear ();
  int max_items = lines () - 4;
  int c = cols () - 4;
  if (max_items > (int)log_.size ()) {
    max_items = (int)log_.size ();
  }
  int p = 0;
  for (auto it = log_.begin (); p < max_items && it != log_.end (); p ++, it ++) {
    std::wstring x = utf8tow (*it);
    td_addstr (2 + p, 2, x, c);
  }
  refresh ();
}

void TdLogWindow::add_log_str (const std::string &str) {
  log_.push_front (str);
  while ((int)log_.size () > max_log_size_) {
    log_.pop_back ();
  }
  redraw ();
}
