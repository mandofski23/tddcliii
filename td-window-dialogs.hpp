#pragma once

#include <string>
#include <set>
#include <unordered_map>

#include "td-window.hpp"
#include "td/utils/tl_storer.h"

class TdDialogWindow : public TdWindow {
  private:
    const long long list_header = (1ull << 63) - 1;

    struct ChatPosition {
      long long order;
      long long chat_id;

      bool operator<(const ChatPosition &r) const {
        return order > r.order || (order == r.order && chat_id > r.chat_id);
      }

      ChatPosition(long long order, long long chat_id) : order(order), chat_id(chat_id) {
      }
    };
    
    std::set<ChatPosition> dialog_list;
    std::unordered_map<long long,long long> chat_order;
    ChatPosition last_position = ChatPosition((1ull << 63) - 1, (1ull << 63) - 1);
    ChatPosition position = ChatPosition((1ull << 63) - 1, (1ull << 63) - 1);
    int position_offset;
    bool started_ = false;
    bool running_query_ = false;
  public:
    TdDialogWindow(int nlines, int ncols, int begin_y, int begin_x) : TdWindow (nlines,ncols,begin_y,begin_x) {
      dialog_list.insert (position);
    }

    void key_pressed (const TermKeyKey *key) override;
    void fn_key_pressed (const TermKeyKey *key) override;
    void sym_key_pressed (const TermKeyKey *key) override;
    void redraw () override;
    
    void updateChat (long long chat_id, long long order);
    void started ();

    void got_dialogs (const td::tl::tl_object_storage<td::td_api::chats> &chats_);
    void got_dialogs_failed ();
};
