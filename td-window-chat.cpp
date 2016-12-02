#include "td/telegram/ClientActor.h"
#include "td/actor/actor.h"
#include "td/utils/FileLog.h"
#include "td/utils/misc.h"
#include "td/utils/tl_storer.h"
#include "auto/tl/td_api.hpp"

#include <cwctype>

#include "td-window-chat.hpp"
#include "telegram-curses.hpp"

extern TdChatWindow *ChatWindow;

class TdOkCallback : public TdQueryCallback {
  public:
    void on_result (td::tl::tl_object_storage<td::td_api::nullary_object> result) override {
    }
    void on_error (td::tl::tl_object_storage<td::td_api::error> error) override {
      td_curses_log ("OPEN_CHAT_ERROR: " + error.get ()->message_);
    }
};

class TdMessagesCallback : public TdQueryCallback {
  public:
    void on_result (td::tl::tl_object_storage<td::td_api::nullary_object> result) override {
      if (ChatWindow && ChatWindow->getChatId () == chat_id) {
        ChatWindow->add_history (td::tl::move_as<td::td_api::messages>(result), first_allowed_message_id);
      }
    }
    void on_error (td::tl::tl_object_storage<td::td_api::error> error) override {
      td_curses_log ("OPEN_CHAT_ERROR: " + error.get ()->message_);

      if (ChatWindow && ChatWindow->getChatId () == chat_id) {
        ChatWindow->get_history_failed (first_allowed_message_id);
      }
    }

    long long chat_id;
    int first_allowed_message_id;
    TdMessagesCallback(long long chat_id, int first_allowed_message_id) : chat_id (chat_id), first_allowed_message_id (first_allowed_message_id) {
    }
};

class TdSendMessageCallback : public TdQueryCallback {
  public:
    void on_result (td::tl::tl_object_storage<td::td_api::nullary_object> result) override {
    }
    void on_error (td::tl::tl_object_storage<td::td_api::error> error) override {
      td_curses_log ("SEND_MESSAGE_ERROR: " + error.get ()->message_);
    }

    long long chat_id;
    TdSendMessageCallback(long long chat_id) : chat_id (chat_id) {
    }
};


//messageText text:string entities:vector<MessageEntity> web_page:webPage = MessageContent;
void print_message_content (const td::td_api::messageText &M, std::vector<std::wstring> &res, int max_width) {
  std::wstring wstr = utf8tow (M.text_);

  std::wstring cur = L"  ";

  for (int i = 0; i < (int)wstr.length (); i++) {
    if (wstr[i] == '\n') {
      res.push_back (cur);
      cur = L"  ";
    } else {
      if ((int)cur.length () >= max_width) {
        res.push_back (cur);
        cur = L"  ";
      }
      wchar_t ch = wstr[i];
      if (ch == '\t') {
        ch = ' ';
      }
      if (iswprint (ch)) {
        cur += ch;
      } else {
        ch = ' ';
        cur += ch;
      }
    }
  }
 
  if (cur != L"  ") {
    res.push_back (cur);
  }
}

//messageAnimation animation:animation caption:string = MessageContent;
void print_message_content (const td::td_api::messageAnimation &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"ANIMATION");
}


//messageAudio audio:audio caption:string is_listened:Bool = MessageContent;
void print_message_content (const td::td_api::messageAudio &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"AUDIO");
}

//messageDocument document:document caption:string = MessageContent;
void print_message_content (const td::td_api::messageDocument &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"DOCUMENT");
}

//messagePhoto photo:photo caption:string = MessageContent;
void print_message_content (const td::td_api::messagePhoto &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"PHOTO");
}

//messageSticker sticker:sticker = MessageContent;
void print_message_content (const td::td_api::messageSticker &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"STICKER");
}

//messageVideo video:video caption:string = MessageContent;
void print_message_content (const td::td_api::messageVideo &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"VIDEO");
}

//messageVoice voice:voice caption:string is_listened:Bool = MessageContent;
void print_message_content (const td::td_api::messageVoice &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"VOICE");
}

//messageLocation location:location = MessageContent;
void print_message_content (const td::td_api::messageLocation &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"LOCATION");
}

//messageVenue venue:venue = MessageContent;
void print_message_content (const td::td_api::messageVenue &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"VENUE");
}

//messageContact contact:contact = MessageContent;
void print_message_content (const td::td_api::messageContact &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"CONTACT");
}

//messageGame game:game = MessageContent;
void print_message_content (const td::td_api::messageGame &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"GAME");
}

//messageGroupChatCreate title:string members:vector<user> = MessageContent;
void print_message_content (const td::td_api::messageGroupChatCreate &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"GROUP_CHAT_CREATE");
}

//messageChannelChatCreate title:string = MessageContent;
void print_message_content (const td::td_api::messageChannelChatCreate &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"CHANNEL_CREATE");
}

//messageChatChangeTitle title:string = MessageContent;
void print_message_content (const td::td_api::messageChatChangeTitle &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"CHANGE_TITLE");
}

//messageChatChangePhoto photo:photo = MessageContent;
void print_message_content (const td::td_api::messageChatChangePhoto &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"CHANGE_PHOTO");
}

//messageChatDeletePhoto = MessageContent;
void print_message_content (const td::td_api::messageChatDeletePhoto &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"DELETE_PHOTO");
}

//messageChatAddMembers members:vector<user> = MessageContent;
void print_message_content (const td::td_api::messageChatAddMembers &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"ADD_MEMBERS");
}

//messageChatJoinByLink inviter_user_id:int = MessageContent;
void print_message_content (const td::td_api::messageChatJoinByLink &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"JOIN_BY_LINK");
}

//messageChatDeleteMember user:user = MessageContent;
void print_message_content (const td::td_api::messageChatDeleteMember &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"DELETE_MEMBER");
}

//messageChatMigrateTo channel_id:int = MessageContent;
void print_message_content (const td::td_api::messageChatMigrateTo &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"MIGRATE_TO");
}

//messageChatMigrateFrom title:string group_id:int = MessageContent;
void print_message_content (const td::td_api::messageChatMigrateFrom &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"MIGRATE_FROM");
}

//messagePinMessage message_id:int = MessageContent;
void print_message_content (const td::td_api::messagePinMessage &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"PIN_MESSAGE");
}

//messageGameScore game_message_id:int game_id:long score:int = MessageContent;
void print_message_content (const td::td_api::messageGameScore &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"GAME_SCORE");
}

//messageUnsupported = MessageContent;
void print_message_content (const td::td_api::messageUnsupported &M, std::vector<std::wstring> &res, int max_width) {
  res.push_back (L"UNSUPPORTED");
}

std::vector<std::wstring> print_message (const td::td_api::message *M, int max_width) {
  std::vector<std::wstring> res;

  std::wstring head = L"";

  if (M->sender_user_id_) {
    if (M->sender_user_id_ == CursesClient::instance_->my_id) {
      head += L"<";
    } else {
      head += L">";
    }
  } else {
    head += L">";
  }

  head += (wchar_t)' ';
  head += td_print_date (M->date_);

  if (M->sender_user_id_) {
    const td::td_api::user *U = CursesClient::instance_->get_user (M->sender_user_id_);
    if (U) {
      if (U->first_name_ != "") {
        head += L" ";    
        head += td_create_escaped_title (U->first_name_);
      }
      if (U->last_name_ != "") {
        head += L" ";    
        head += td_create_escaped_title (U->last_name_);
      }
    } else {
      head += L" UNKNOWN USER";    
    }
  }

  res.push_back (head);
  

  M->content_.get ()->call ([&](auto &object) { print_message_content (object, res, max_width); });

  return res;
}

TdChatWindow::TdChatWindow(long long chat_id, int nlines, int ncols, int begin_y, int begin_x) : TdWindow (nlines,ncols,begin_y,begin_x), chat_id_(chat_id), line_edit(1, 5, ncols - 4, true) {
  if (chat_id_ == 0) {
    return;
  }
  CursesClient::instance_->send_request (td::tl::create_tl_object<td::td_api::openChat>(chat_id_), td::make_unique<TdOkCallback>());
  redraw ();
}

TdChatWindow::~TdChatWindow() {
  CursesClient::instance_->send_request (td::tl::create_tl_object<td::td_api::closeChat>(chat_id_), td::make_unique<TdOkCallback>());
}
    
void TdChatWindow::add_message (td::tl::tl_object_storage<td::td_api::message> message) {
  auto M = static_cast<const td::td_api::message *>(message.get ());

  int id = M->id_;

  messages_[-id] = std::move(message);
  
  redraw ();
}
    
void TdChatWindow::get_history_failed (int first_allowed_message_id) {
  if (first_allowed_message_id != first_allowed_message_id_) {
    return;
  }
  
  query_running_ = false;
  redraw ();
}
    
void TdChatWindow::add_history (td::tl::tl_object_storage<td::td_api::messages> messages, int first_allowed_message_id) {
  if (first_allowed_message_id != first_allowed_message_id_) {
    return;
  }
  auto M = static_cast<td::td_api::messages *>(messages.get ());

  int size = (int)M->messages_.size ();
  if (size > 0) {
    auto msg = static_cast<const td::td_api::message *>(M->messages_[size - 1].get ());
    int id = msg->id_;
    
    for (int i = 0; i < size; i++) {
      add_message (std::move (M->messages_[i]));
    }

    first_allowed_message_id_ = id;
  } else {
    first_allowed_message_id_ = 0;
  }
  
  query_running_ = false;
  redraw ();
}
    
void TdChatWindow::update_message_content (int message_id, td::tl::tl_object_storage<td::td_api::MessageContent> content) {
  auto it = messages_.find (-message_id);
  if (it == messages_.end ()) {
    return;
  }
  it->second->content_ = std::move (content);
  redraw ();
}

void TdChatWindow::update_message_edited (int message_id, int edit_date, td::tl::tl_object_storage<td::td_api::ReplyMarkup> reply_markup) {
  auto it = messages_.find (-message_id);
  if (it == messages_.end ()) {
    return;
  }
  it->second->edit_date_ = edit_date;
  it->second->reply_markup_ = std::move (reply_markup);

  redraw ();
}

void TdChatWindow::update_message_views (int message_id, int views) {
  auto it = messages_.find (-message_id);
  if (it == messages_.end ()) {
    return;
  }
  it->second->views_ = views;

  redraw ();
}

void TdChatWindow::update_user_action (int user_id, const td::tl::tl_object_storage<td::td_api::SendMessageAction> &action) {
}

void TdChatWindow::delete_message (int message_id) {
  auto it = messages_.find (-message_id);
  if (it == messages_.end ()) {
    return;
  }
  if (bottom_message_id_ == message_id) {
    if (it != messages_.begin ()) {
      it --;
      bottom_message_id_ = it->first;
      bottom_message_line_num_ = 0;
      it ++;
    } else {
      it ++;
      if (it != messages_.end ()) {
        bottom_message_id_ = it->first;
        bottom_message_line_num_ = INT_MAX;
      } else {
        bottom_message_id_ = 0;
        bottom_message_line_num_ = 0;
      }
      it --;
    }
  }
  messages_.erase (it);

  redraw ();
}
    
void TdChatWindow::redraw () {
  clear ();
  
  int width = cols () - 4;

  int cur_x, cur_y;
  int r = line_edit.draw (this, lines () - 2, 2, true, &cur_y, &cur_x, true);
  int y = lines () - 2 - r;

  auto it = messages_.find (-bottom_message_id_);
  if (it == messages_.end ()) {
    it = messages_.begin ();
    bottom_message_id_ = INT_MAX;
    bottom_message_line_num_ = INT_MAX;
  }
  
  while (it != messages_.end ()) {
    auto msg = static_cast<const td::td_api::message *>(it->second.get ());
    std::vector<std::wstring> X = print_message (msg, width);

    int x = (int)X.size ();
    if (msg->id_ == bottom_message_id_) {
      if (x <= bottom_message_line_num_) {
        bottom_message_line_num_ = x - 1;
      } else {
        x = bottom_message_line_num_ + 1;
      }
    }
    while (y > 0 && x > 0) {
      td_addstr (y, 2, X[x - 1], width);
      x --;
      y --;
    }

    if (!y) {
      break;
    }

    it ++;
  }

  if (y > 0 && first_allowed_message_id_ > 0 && !query_running_) {
    CursesClient::instance_->send_request (td::tl::create_tl_object<td::td_api::getChatHistory>(chat_id_, first_allowed_message_id_, 0, 10), td::make_unique<TdMessagesCallback>(chat_id_, first_allowed_message_id_));
    query_running_ = true;
  }

  td_move (cur_y, cur_x);    
  refresh ();
}

void TdChatWindow::fn_key_pressed (const TermKeyKey *key) {
  if (bottom_message_id_ == INT_MAX) {
    line_edit.fn_process_key (key);
    redraw ();
  }
}
    
void TdChatWindow::sym_key_pressed (const TermKeyKey *key) {
  if (key->code.sym == TERMKEY_SYM_ESCAPE) {
    ChatWindow = nullptr;
    delete this;
    return;
  }
  if (line_edit.get_wstring ().length () > 0) {
    if (key->code.sym == TERMKEY_SYM_ENTER && (key->modifiers & TERMKEY_KEYMOD_ALT)) {
      std::string str = line_edit.get_string ();
      CursesClient::instance_->send_request (td::tl::create_tl_object<td::td_api::sendMessage>(chat_id_, 0, false, false, nullptr, td::tl::create_tl_object<td::td_api::inputMessageText>(str, false, false, std::vector<td::tl::tl_object_storage<td::td_api::MessageEntity>>(), nullptr)), td::make_unique<TdSendMessageCallback>(chat_id_));
      line_edit.clear ();

      redraw ();
    } else {
      line_edit.sym_process_key (key);
      redraw ();
    }
    return;
  }
  if (key->code.sym == TERMKEY_SYM_UP) {
    int width = cols () - 4;

    auto it = messages_.find (-bottom_message_id_);
    if (it == messages_.end ()) {
      it = messages_.begin ();
      bottom_message_line_num_ = INT_MAX;
      if (it == messages_.end ()) {
        bottom_message_id_ = INT_MAX;
        redraw ();
        return;
      }
      bottom_message_id_ = -it->first;
    }
    if (bottom_message_line_num_ == 0) {
      it ++;
      if (it != messages_.end ()) {
        bottom_message_id_ = -it->first;
        bottom_message_line_num_ = INT_MAX;
      } else {
        it --; 
        bottom_message_line_num_ ++;
      }
    }
    

    bottom_message_line_num_ --;
    auto M = static_cast<const td::td_api::message *>(it->second.get ());

    std::vector<std::wstring> X = print_message (M, width);
    if (bottom_message_line_num_ >= (int)X.size ()) {
      bottom_message_line_num_ = (int)X.size () - 1;
    }

    redraw ();
  } else if (key->code.sym == TERMKEY_SYM_DOWN) {
    int width = cols () - 4;

    auto it = messages_.find (-bottom_message_id_);
    if (it == messages_.end ()) {
      bottom_message_line_num_ = INT_MAX;
      bottom_message_id_ = INT_MAX;
      redraw ();
      return;
    }
    
    auto M = static_cast<const td::td_api::message *>(it->second.get ());
    std::vector<std::wstring> X = print_message (M, width);
    if (bottom_message_line_num_ < (int)X.size () - 1) {
      bottom_message_line_num_ ++;
    } else {
      if (it == messages_.begin ()) {
        bottom_message_line_num_ = INT_MAX;
        bottom_message_id_ = INT_MAX;
      } else { 
        it --;
        bottom_message_line_num_ = 0;
        bottom_message_id_ = -it->first;
      }
    }

    redraw ();
  } else if (key->code.sym == TERMKEY_SYM_END) {
    bottom_message_line_num_ = INT_MAX;
    bottom_message_id_ = INT_MAX;

    redraw ();
  } else if (key->code.sym == TERMKEY_SYM_ESCAPE) {
    delete this;
  }
}
    
void TdChatWindow::key_pressed (const TermKeyKey *key) {
  if (bottom_message_id_ == INT_MAX) {
    line_edit.process_key (key);
    redraw ();
  }
}
