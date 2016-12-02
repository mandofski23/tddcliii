#include "td/telegram/ClientActor.h"
#include "td/actor/actor.h"
#include "td/utils/FileLog.h"
#include "td/utils/misc.h"
#include "td/utils/tl_storer.h"

#include <string>

#include "td-window-input.hpp"
#include "td-curses-utils.hpp"

#include "telegram-curses.hpp"
    
void TdInputWindow::key_pressed (const TermKeyKey *key) {
  result[pos].process_key (key);
  redraw ();
}
    
void TdInputWindow::fn_key_pressed (const TermKeyKey *key) {
  result[pos].fn_process_key (key);
  redraw ();
}
    
void TdInputWindow::sym_key_pressed (const TermKeyKey *key) {
  if (key->code.sym == TERMKEY_SYM_ENTER) {
    if (pos + 1 < (int)prompt.size ()) {
      pos ++;
      redraw ();
    } else {
      run ();
    }
  } else {
    result[pos].sym_process_key (key);
    redraw ();
  }
}

void TdInputWindow::redraw () {
  clear ();
  int cx = 0;
  int cy = 0;
  for (size_t i = 0; i < prompt.size (); i++) {
    td_addstr (2 + 2 * (int)i, 2, prompt[i]);
    int x, y;
    result[i].draw (this, 2 + 2 * (int)i, 2 + (int)prompt[i].length (), true, &y, &x, pos == (int)i); 
    if (pos == (int)i) {
      cy = y;
      cx = x;
    }
  }
  td_move (cy, cx);
  refresh ();
}
    
std::string TdInputWindow::get_result (int id) {
  return result[id].get_string ();
}

void TdInputWindowPhone::run () {
  std::string s = get_result (0);
  CursesClient::instance_->send_request (td::tl::create_tl_object<td::td_api::setAuthPhoneNumber>(s, false, false), td::make_unique<CursesClient::TdAuthStateCallback>());
  delete this;
}

void TdInputWindowCode::run () {
  std::string s = get_result (0);
  CursesClient::instance_->send_request (td::tl::create_tl_object<td::td_api::checkAuthCode>(s, "", ""), td::make_unique<CursesClient::TdAuthStateCallback>());
  delete this;
}

void TdInputWindowCodeReg::run () {
  std::string s = get_result (0);
  std::string x = get_result (1);
  std::string y = get_result (2);
  CursesClient::instance_->send_request (td::tl::create_tl_object<td::td_api::checkAuthCode>(s, x, y), td::make_unique<CursesClient::TdAuthStateCallback>());
  delete this;
}

void TdInputWindowPassword::run () {
  std::string s = get_result (0);  
  CursesClient::instance_->send_request (td::tl::create_tl_object<td::td_api::checkAuthPassword>(s), td::make_unique<CursesClient::TdAuthStateCallback>());
  delete this;
}
