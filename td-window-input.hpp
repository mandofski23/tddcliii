#pragma once

#include <string>
#include <vector>

#include "td-window.hpp"
#include "td-window-root.hpp"
#include "td-line-edit.hpp"

class TdInputWindow : public TdWindow {
  private:
    std::vector<std::wstring> prompt;
    std::vector<TdLineEdit> result;
    int pos = 0;
  public:
    TdInputWindow(std::vector<std::wstring> &&prompt, bool echo, int nlines, int ncols, int begin_y, int begin_x) : TdWindow (nlines,ncols,begin_y,begin_x), prompt(prompt) {
      result = std::vector<TdLineEdit>();
      for (size_t i = 0; i < prompt.size (); i++) {
        result.push_back (TdLineEdit (1, 1, ncols - 4 - (int)prompt[i].length (), echo));
      }
      redraw ();
    }
   
    std::string get_result (int id);
    void redraw () override;
    /*void key_pressed (wint_t key) override;
    void fn_key_pressed (wint_t key) override;*/
    void key_pressed (const TermKeyKey *key) override;
    void fn_key_pressed (const TermKeyKey *key) override;
    void sym_key_pressed (const TermKeyKey *key) override;

    virtual void run () {
    }
};

class TdInputWindowPhone : public TdInputWindow {
  public:
    TdInputWindowPhone() : TdInputWindow ({L"phone: +"}, true, 5, Root->cols (), Root->lines () / 2 - 2, 0) {
    }

    void run () override;

    ~TdInputWindowPhone() override {
    }
};

class TdInputWindowCode : public TdInputWindow {
  public:
    TdInputWindowCode() : TdInputWindow ({L"code: "}, true, 5, Root->cols (), Root->lines () / 2 - 2, 0) {
    }

    void run () override;

    ~TdInputWindowCode() override {
    }
};

class TdInputWindowCodeReg : public TdInputWindow {
  public:
    TdInputWindowCodeReg() : TdInputWindow ({L"      code: ", L"first name: ", L" last name: "}, true, 9, Root->cols (), Root->lines () / 2 - 4, 0) {
    }

    void run () override;

    ~TdInputWindowCodeReg() override {
    }
};

class TdInputWindowPassword : public TdInputWindow {
  public:
    TdInputWindowPassword() : TdInputWindow ({L"password: "}, false, 5, Root->cols (), Root->lines () / 2 - 2, 0) {
    }

    void run () override;

    ~TdInputWindowPassword() override {
    }
};
