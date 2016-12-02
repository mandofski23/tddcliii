#include <cctype>
#include <cwctype>
#include <vector>

#include "ncursesw/ncurses.h"

#include "td-line-edit.hpp"

std::wstring TdLineEdit::get_wstring () {
  return state;
}

std::string TdLineEdit::get_string () {
  return wtoutf8 (state);
}

void TdLineEdit::try_move_down () {
  int len = (int)state.length ();
 
  while (pos < len) {
    if (state[pos] == '\n') {
      break;
    }
    pos ++;
  }

  if (pos == len) {
    return;
  }
  
  pos ++;
}

void TdLineEdit::try_move_up () {
  pos --;
  while (pos >= 0) {
    if (state[pos] == '\n') {
      break;
    }
    pos --;
  }

  if (pos < 0) {
    pos = 0;
  }
}
    
void TdLineEdit::try_move_left () {
  if (pos > 0 && state[pos - 1] != '\n') {
    pos --;
  }  
}
    
void TdLineEdit::try_move_right () {
  int len = (int)state.length ();
  
  if (pos < len && state[pos] != '\n') {
    pos ++;
  }  
}

void TdLineEdit::try_move_line_begin () {
  int len = (int)state.length ();
  
  while (pos >= 0) {
    if (pos != len && state[pos] == '\n') {
      break;
    }
    pos --;
  }

  pos ++;
}

void TdLineEdit::try_backspace () {
  if (pos > 0) {
    state.erase (pos - 1, 1);
    pos --;
  }
}

void TdLineEdit::try_delete_line () {
}

void TdLineEdit::try_delete () {
  int len = (int)state.length ();
 
  if (pos < len) {
    state.erase (pos, 1);
  }
}

void TdLineEdit::try_page_up () {
  for (int i = 0; i < 10; i++) {
    if (pos == 0) {
      return;
    }
    try_move_up ();
  }
}

void TdLineEdit::try_page_down () {
  int len = (int)state.length ();
 
  for (int i = 0; i < 10; i++) {
    if (pos == len) {
      return;
    }
    try_move_down ();
  }
}

void TdLineEdit::try_move_line_end () {
  int len = (int)state.length ();

  while (pos < len) {
    if (state[pos] == '\n') {
      break;
    }
    pos ++;
  }
}

void TdLineEdit::process_key (const TermKeyKey *key) {
  std::string str(key->utf8);

  std::wstring wstr = utf8tow (str);
  state.insert ((size_t)pos, wstr);

  pos += (int)wstr.length ();
}

void TdLineEdit::fn_process_key (const TermKeyKey *key) {
}

void TdLineEdit::sym_process_key (const TermKeyKey *key) {
  wchar_t ch;
  switch (key->code.sym) {
    case TERMKEY_SYM_BACKSPACE:
      try_backspace ();
      break;
    case TERMKEY_SYM_TAB:
      ch = (wchar_t)'\t';
      state.insert ((size_t)pos, &ch, 1);
      pos ++;
      break;
    case TERMKEY_SYM_ENTER:
      ch = (wchar_t)'\n';
      state.insert ((size_t)pos, &ch, 1);
      pos ++;
      break;
    case TERMKEY_SYM_SPACE:
      ch = (wchar_t)' ';
      state.insert ((size_t)pos, &ch, 1);
      pos ++;
      break;
    case TERMKEY_SYM_DEL:
    case TERMKEY_SYM_DELETE:
      try_delete ();
      break;
    case TERMKEY_SYM_UP:
      try_move_up ();
      break;
    case TERMKEY_SYM_DOWN:
      try_move_down ();
      break;
    case TERMKEY_SYM_LEFT:
      try_move_left ();
      break;
    case TERMKEY_SYM_RIGHT:
      try_move_right ();
      break;
    case TERMKEY_SYM_HOME:
      try_move_line_begin ();
      break;
    case TERMKEY_SYM_END:
      try_move_line_end ();
      break;
    case TERMKEY_SYM_PAGEUP:
      try_page_up ();
      break;
    case TERMKEY_SYM_PAGEDOWN:
      try_page_down ();
      break;
    default:
      break;
  }
}

int TdLineEdit::draw (TdWindow *W, int y, int x, bool up, int *cur_y, int *cur_x, bool enable_cursor) {
  if (!echo) {
    *cur_y = y;
    *cur_x = x;
    return min_block_height;
  }
  std::vector<std::wstring> V;

  std::wstring cur = L"";

  int len = (int)state.length ();
  int pos_line = -1;
  int pos_pos = -1;
  for (int p = 0; p < len; p++) {
    if (state[p] == '\n') {
      if (p == pos && enable_cursor) {
        if ((int)cur.length () == block_width) {
          V.push_back (cur);
          cur = L"";
        }
        pos_line = (int)V.size ();
        pos_pos = (int)cur.length ();
        cur = cur + (wchar_t)' ';
      }
      V.push_back (cur);
      cur = L"";
    } else {
      wchar_t ch = state[p];
      if (ch == '\t') {
        ch = ' ';
      }      
      if (p == pos && enable_cursor) {
        pos_line = (int)V.size ();
        pos_pos = (int)cur.length ();
      }
      cur = cur + ch;

      if ((int)cur.length () == block_width) {
        V.push_back (cur);
        cur = L"";
      }
    }
  }

  if (len == pos) {
    if ((int)cur.length () == block_width) {
      V.push_back (cur);
      cur = L"";
    }
    pos_line = (int)V.size ();
    pos_pos = (int)cur.length ();
    cur = cur + (wchar_t)' ';
  }
      
  V.push_back (cur);

  int total_lines = (int)V.size ();


  if (frame_start_line > pos_line) {
    frame_start_line = pos_line;
  }
  if (pos_line - frame_start_line >= max_block_height) {
    frame_start_line  = pos_line - max_block_height + 1;
  }

  if (frame_start_line > total_lines - max_block_height) {
    frame_start_line = total_lines - max_block_height;
    if (frame_start_line < 0) {
      frame_start_line = 0;
    }
  }

  int lines = total_lines - frame_start_line;
  if (lines > max_block_height) {
    lines = max_block_height;
  }

  int s = lines > min_block_height ? lines : min_block_height;

  int i;
  for (i = 0; i < lines; i++) {
    W->td_addstr (y + (up ? -s + 1 : 0) + i, x, V[frame_start_line + i]);
  }
  for (i = lines; i < min_block_height; i++) {
    std::wstring emp = L"";
    W->td_addstr (y + (up ? -s + 1: 0) + i, x, emp);
  }

  *cur_y = y + (up ? -s + 1 : 0) + (pos_line - frame_start_line);
  *cur_x = x + pos_pos;

  return (lines > min_block_height ? lines : min_block_height);
}
