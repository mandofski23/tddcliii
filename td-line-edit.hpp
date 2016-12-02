#pragma once

#include <string>
#include <termkey.h>
#include "td-curses-utils.hpp"
#include "td-window.hpp"

class TdLineEdit {
  private:
    std::wstring state = L"";
    int frame_start_line = 0;
    int pos = 0;
    int min_block_height;
    int max_block_height;
    int block_width;
    bool echo;

    void try_move_down ();
    void try_move_up ();
    void try_move_left ();
    void try_move_right ();
    void try_move_line_begin ();
    void try_backspace ();
    void try_delete_line ();
    void try_delete ();
    void try_page_up ();
    void try_page_down ();
    void try_move_line_end ();

  public:
    std::wstring get_wstring ();
    std::string get_string ();

    void clear () {
      pos = 0;
      frame_start_line = 0;
      state = L"";
    }

    void process_key (const TermKeyKey *key);
    void fn_process_key (const TermKeyKey *key);
    void sym_process_key (const TermKeyKey *key);

    int draw (TdWindow *W, int y, int x, bool up, int *cur_y, int *cur_x, bool enable_cursor); 

    TdLineEdit (int min_block_height, int max_block_height, int block_width, bool echo) :
      min_block_height (min_block_height),
      max_block_height (max_block_height),
      block_width (block_width),
      echo (echo) {
    }
};
