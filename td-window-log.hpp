#pragma once

#include <list>
#include <string>

#include "td-window.hpp"

class TdLogWindow : public TdWindow {
  private:
    std::list<std::string> log_;
    int max_log_size_ = 100;
  public:
    TdLogWindow(int nlines, int ncols, int begin_y, int begin_x) : TdWindow (nlines,ncols,begin_y,begin_x) {
    }

    void key_pressed (const TermKeyKey *key) override {
    }
    void fn_key_pressed (const TermKeyKey *key) override {
    }
    void sym_key_pressed (const TermKeyKey *key) override {
    }
    void redraw () override;
    void add_log_str (const std::string &str);
    void updateChatPosition (long long chat_id);
};
