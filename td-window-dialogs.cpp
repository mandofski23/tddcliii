#include "td/telegram/ClientActor.h"
#include "td/actor/actor.h"
#include "td/utils/FileLog.h"
#include "td/utils/misc.h"
#include "td/utils/tl_storer.h"

#include "td-window-dialogs.hpp"
#include "td-window-chat.hpp"
#include "td-window-root.hpp"
#include "telegram-curses.hpp"

extern TdChatWindow *ChatWindow;

void TdDialogWindow::updateChat (long long chat_id, long long order) {
  {
    auto it = chat_order.find (chat_id);

    if (it != chat_order.end ()) {
      if (it->second == order) {
        return;
      }

      ChatPosition P(it->second,chat_id);
      dialog_list.erase (P);
      chat_order.erase (it);
    }
  }
  
  ChatPosition Q(order,chat_id);
  
  if (order > 0) {
    auto it = dialog_list.find (Q);

    if (it == dialog_list.end ()) {
      dialog_list.insert (Q);
    }

    chat_order[chat_id] = order;
  }

  if (position.chat_id == chat_id) {
    if (order > 0) {
      position = ChatPosition (order,chat_id);
    } else {
      position = ChatPosition ((1ull << 63) - 1, (1ull << 63) - 1);
      position_offset = 0;
    }
  }

  redraw ();
}

void TdDialogWindow::started () {
  started_ = true;
  redraw ();
}

class TdChatsCallback : public TdQueryCallback {
  public:
  void on_result (td::tl::tl_object_storage<td::td_api::nullary_object> result) override {
    td_curses_log ("GET_CHATS_SUCCESS");
    W->got_dialogs (td::tl::move_as<td::td_api::chats>(result));
  }
  void on_error (td::tl::tl_object_storage<td::td_api::error> error) override {
    td_curses_log ("GET_CHATS_ERROR: " + error.get ()->message_);
    W->got_dialogs_failed ();
  }

  TdChatsCallback (TdDialogWindow *W) : W(W) {}
  TdDialogWindow *W;
};

void TdDialogWindow::redraw () {
  clear ();

  int max_cols = cols () - 4;
  int max_size = lines () - 2;
  assert (max_size >= 1);
  if (position_offset >= max_size) {
    position_offset = max_size - 1;
  }

  int need_get = true;
  auto it = dialog_list.find (position);
  if (it == dialog_list.end ()) {
    assert (!position_offset);

    it = dialog_list.begin ();
  }

  int i = 0;
  for (i = 0; i < position_offset && it != dialog_list.begin (); i++, it--) {}
  position_offset = i;

  int p = 0;
  for (; p < max_size; p++) {
    ChatPosition P = *it;

    if (last_position < P) {
      break;
    }

    if (P.order == list_header) {
      std::wstring dname = L"Dialog list";
      td_addstr (1 + p, 2, dname, max_cols);
    } else {
      const td::td_api::chat *C = CursesClient::instance_->get_chat (P.chat_id);

      std::wstring name;
      if (!C) {
        name = L"UNKNOWN CHAT";
      } else {
        name = td_create_escaped_title (C->title_);
      }

      td_addstr (1 + p, 2, name, max_cols);
    }

    it ++;
    if (it == dialog_list.end ()) {
      break;
    }
  }

  if (p == max_size) {
    need_get = false;
  }
  
  if (need_get && last_position.order >= 0 && !running_query_) {
    CursesClient::instance_->send_request (td::tl::create_tl_object<td::td_api::getChats>(last_position.order, last_position.chat_id, max_size), td::make_unique<TdChatsCallback>(this));

    running_query_ = true;
  }

  td_move (1 + position_offset, 1);
  refresh ();
}
    
void TdDialogWindow::got_dialogs (const td::tl::tl_object_storage<td::td_api::chats> &chats_) {
  auto chats = static_cast<const td::td_api::chats *>(chats_.get ());

  td_curses_log ("GOT " + std::to_string (chats->chats_.size ()) + " chats");
  if (chats->chats_.size () == 0) {
    last_position.order = -1;
    running_query_ = false;
    redraw ();
  } else {
    for (size_t i = 0; i < chats->chats_.size (); i++) {
      auto chat = static_cast<const td::td_api::chat *>(chats_->chats_[i].get ());
      ChatPosition P(chat->order_, chat->id_);

      if (last_position < P) {
        last_position = P;
      }
    }
    
    running_query_ = false;
    redraw ();
  }
}

void TdDialogWindow::got_dialogs_failed () {
  running_query_ = false;
  redraw ();
}
    
void TdDialogWindow::key_pressed (const TermKeyKey *key) {
}

void TdDialogWindow::fn_key_pressed (const TermKeyKey *key) {
}

void TdDialogWindow::sym_key_pressed (const TermKeyKey *key) {
  if (key->code.sym == TERMKEY_SYM_DOWN) { 
    auto it = dialog_list.find (position);
    it ++;

    if (it != dialog_list.end ()) {
      position_offset ++;
      position = *it;

      redraw ();
    } else if (last_position.order >= 0) {
      int max_size = lines () - 2;
      if (position_offset == max_size - 1) {
        position_offset --;
      }

      redraw ();
    }
  } else if (key->code.sym == TERMKEY_SYM_UP) { 
    auto it = dialog_list.find (position);

    if (it != dialog_list.begin ()) {
      it --;
      if (position_offset > 0) {
        position_offset --;
      }
      position = *it;

      redraw ();
    }
  } else if (key->code.sym == TERMKEY_SYM_ENTER) {
    auto it = dialog_list.find (position);

    if (it != dialog_list.end ()) {
      if (ChatWindow) {
        delete ChatWindow;
      }
      ChatWindow = new TdChatWindow (it->chat_id, lines (), Root->cols () - cols (), 0, cols ());

      redraw ();
    }
  }
}
