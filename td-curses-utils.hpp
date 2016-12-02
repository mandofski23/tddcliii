#pragma once

#include <string>

void td_curses_log (const std::string &&str);
std::string wtoutf8 (const std::wstring &wstr);
std::wstring utf8tow (const std::string &str);
std::wstring td_create_escaped_title (const std::string &str);
std::wstring td_print_date (int date);
