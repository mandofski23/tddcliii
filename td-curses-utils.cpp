#include <locale>
#include <codecvt>
#include <iomanip>
#include <cstdio>

#include "td-curses-utils.hpp"

std::wstring_convert<std::codecvt_utf8<wchar_t>> conv_utf8;

std::string wtoutf8 (const std::wstring &wstr) {
  return conv_utf8.to_bytes (wstr);
}

std::wstring utf8tow (const std::string &str) {
  return conv_utf8.from_bytes (str);
}

std::wstring td_print_date (int date_) {

  std::time_t date = date_;
  auto t_date = std::localtime (&date);

  char buf[512];
  std::sprintf (buf, "[%04d/%02d/%02d %02d:%02d:%02d]", 
    t_date->tm_year + 1900,
    t_date->tm_mon + 1,
    t_date->tm_mday,
    t_date->tm_hour,
    t_date->tm_min,
    t_date->tm_sec
  );
  

  return utf8tow (std::string (buf));
}
