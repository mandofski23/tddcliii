#include "td/telegram/ClientActor.h"
#include "td/actor/actor.h"
#include "td/utils/FileLog.h"
#include "td/utils/misc.h"
#include "td/utils/tl_storer.h"
#include "auto/tl/td_api.hpp"

#include <cstddef>
#include <ncursesw/ncurses.h>
#include <ncursesw/panel.h>
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <iostream>
#include <string>
#include <cwctype>
#include <time.h>

#include "telegram-curses.hpp"
#include "td-window-input.hpp"
#include "td-window-root.hpp"
#include "td-window-log.hpp"
#include "td-window-dialogs.hpp"
#include "td-window-chat.hpp"
#include "td-curses-utils.hpp"


TdDialogWindow *DialogWindow;
TdChatWindow *ChatWindow;
TdLogWindow *LogWindow;

TermKey *tk;

void td_curses_log (const std::string &&str) {
  if (LogWindow) {
    std::string N = std::to_string (time (0)) + " " + str;
    LogWindow->add_log_str (N);
  }
}

std::wstring td_create_escaped_title (const std::string &str) {
  std::wstring wstr = utf8tow (str);
  std::wstring res = L"";
  for (size_t i = 0; i < wstr.length (); i++) {
    if (iswprint (wstr[i])) {
      res += wstr[i];      
    } else {
      res += (wchar_t)' ';
    }
  }
  return res;
}

CursesClient *CursesClient::instance_ = nullptr;
 
const td::td_api::chat *CursesClient::get_chat (long long chat_id) {
  auto it = cid2chat_.find (chat_id);

  if (it == cid2chat_.end ()) {
    return nullptr;
  }

  return static_cast<const td::td_api::chat *>(it->second.get ());
}
 
const td::td_api::user *CursesClient::get_user (int user_id) {
  auto it = uid2user_.find (user_id);

  if (it == uid2user_.end ()) {
    return nullptr;
  }

  return static_cast<const td::td_api::user *>(it->second.get ());
}
 
const td::td_api::group *CursesClient::get_group (int group_id) {
  auto it = gid2group_.find (group_id);

  if (it == gid2group_.end ()) {
    return nullptr;
  }

  return static_cast<const td::td_api::group *>(it->second.get ());
}
 
const td::td_api::channel *CursesClient::get_channel (int channel_id) {
  auto it = chid2channel_.find (channel_id);

  if (it == chid2channel_.end ()) {
    return nullptr;
  }

  return static_cast<const td::td_api::channel *>(it->second.get ());
}
 
const td::td_api::secretChat *CursesClient::get_secret_chat (int secret_chat_id) {
  auto it = scid2secret_chat_.find (secret_chat_id);

  if (it == scid2secret_chat_.end ()) {
    return nullptr;
  }

  return static_cast<const td::td_api::secretChat *>(it->second.get ());
}

void CursesClient::authentificate_restart () {
  switch (auth_state_) {
    case td::td_api::authStateOk::ID:
      td_curses_log ("AUTH SUCCESS");
      DialogWindow->started ();
      break;
    case td::td_api::authStateWaitPhoneNumber::ID:
      td_curses_log ("AUTH WAIT PHONE NUMBER");
      new TdInputWindowPhone ();
      break;
    case td::td_api::authStateLoggingOut::ID:
      td_curses_log ("AUTH LOGGING OUT");
      break;
    case td::td_api::authStateWaitCode::ID:
      td_curses_log ("AUTH WAIT CODE");
      new TdInputWindowCode ();
      break;
    case td::td_api::authStateWaitPassword::ID:
      td_curses_log ("AUTH WAIT PASSWORD");
      new TdInputWindowPassword ();
      break;
    default:
      td_curses_log ("UNKNOWN AUTH STATE ");
      send_request (td::tl::create_tl_object<td::td_api::getAuthState>(), td::make_unique<TdAuthStateCallback>());
      break;
  }
}

void CursesClient::authentificate_continue (td::tl::tl_object_storage<td::td_api::AuthState> result) {
  auth_state_ = result->get_id ();
  authentificate_restart ();
}

  
std::unique_ptr<td::ClientActor::Callback> CursesClient::make_td_callback() {
  class TdCallback : public td::ClientActor::Callback {
    public:
      explicit TdCallback(CursesClient *client) : client_(client) {
      }
      void on_result(td::uint64 id, td::tl::tl_object_storage<td::td_api::nullary_object> result) override {
        client_->on_result(id, std::move(result));
      }
      void on_error(td::uint64 id, td::tl::tl_object_storage<td::td_api::error> error) override {
        client_->on_error(id, std::move(error));
      }
      void on_before_close() override {
        client_->on_before_close();
      }
      void on_closed() override {
        client_->on_closed();
      }

    private:
      CursesClient *client_;
  };
  return std::make_unique<TdCallback>(this);
}
  
void CursesClient::init_td() {
  td::TdParameters parameters;
  parameters.is_test_dc = is_test_dc_;
  parameters.api_id = 12183;
  parameters.api_hash = "41c3080d9028cf002792a512d4e20089";
  parameters.language_code = "en";        // TODO
  parameters.device_model = "Desktop";    // TODO
  parameters.system_version = "Unknown";  // TODO
  parameters.app_version = "tg-cli";
  parameters.use_auto_scheduler_id = true;
  parameters.use_secret_chats = true;

  td_ = td::create_actor<td::ClientActor>("TD-proxy", make_td_callback(), parameters);
}
  
void CursesClient::loop() {
  if (!inited_) {
    inited_ = true;
    init();
  }
  if (can_read(stdin_)) {
    termkey_advisereadable (tk);
    while (1) {
      TermKeyKey key;
      int res = termkey_getkey (tk, &key);
      if (res == TERMKEY_RES_AGAIN) {
        res = termkey_waitkey (tk, &key);
      }
      td_curses_log ("XX " + std::to_string (res) + " " + std::to_string (key.type));
      if (res != TERMKEY_RES_KEY) {
        break;
      }
      TdWindow *W = reinterpret_cast<TdWindow *>(const_cast<void *>(panel_userptr (input_panel)));
  
      if (key.type == TERMKEY_TYPE_UNICODE) {
        W->key_pressed (&key);
      } else if (key.type == TERMKEY_TYPE_FUNCTION) {
        W->fn_key_pressed (&key);
      } else if (key.type == TERMKEY_TYPE_KEYSYM) {
        W->sym_key_pressed (&key);
      }
    }
    stdin_.get_fd().clear_flags(td::Fd::Read);
  }

  if (ready_to_stop_) {
    td::Scheduler::instance()->finish();
    stop();
  }
}

void CursesClient::on_result (td::uint64 id, td::tl::tl_object_storage<td::td_api::nullary_object> result) {
  if (id == 0) {
    on_update (td::tl::move_as<td::td_api::Update>(result));
    return;
  }   

  auto *handler_ptr = handlers_.get(id);
  CHECK(handler_ptr != nullptr);
  auto handler = std::move(*handler_ptr);
  handler->on_result(std::move(result));
  handlers_.erase(id);
}

void CursesClient::on_error (td::uint64 id, td::tl::tl_object_storage<td::td_api::error> error) {   
  //LOG(INFO) << "on_error [id=" << id << "] " << tl::to_string(error);
  auto *handler_ptr = handlers_.get(id);
  CHECK(handler_ptr != nullptr);
  auto handler = std::move(*handler_ptr);
  handler->on_error(std::move (error));
  handlers_.erase(id);
}

/*void CursesClient::update_chat_title (long long chat_id, std::string title) {
  auto it = cid2chat_.find (chat_id);

  if (it == cid2chat_.end ()) {
    return;
  }

  auto chat = static_cast<td::td_api::chat *>(it->second.get ());
  chat->title_ = title;
  
  DialogWindow->redraw ();
}


void CursesClient::update_chat (td::tl::tl_object_storage<td::td_api::updateChat> update_) {
  auto update = static_cast<td::td_api::updateChat *>(update_.get ());

  auto chat = static_cast<const td::td_api::chat *>(update->chat_.get ());

  long long chat_id = chat->id_;
  long long order = chat->order_;
    
  cid2chat_[chat->id_] = std::move (chat_);

  DialogWindow->updateChat (chat_id, order);

  if (ChatWindow && ChatWindow->getChatId () == chat_id) {
    ChatWindow->redraw ();
  }
}

void CursesClient::update_option (td::tl::tl_object_storage<td::td_api::updateChat> update_) {
  auto update = static_cast<td::td_api::updateChat *>(update_.get ());

  update_chat (std::move (update->chat_));
}*/

TdChatWindow *get_chat_window (long long chat_id) {
  if (ChatWindow && ChatWindow->getChatId () == chat_id) {
    return ChatWindow;
  }
  return nullptr;
}

void CursesClient::update_handler (td::td_api::updateNewMessage &update) {  
  auto M_ = std::move (update.message_);
  auto M = static_cast<td::td_api::message *>(M_.get ());

  TdChatWindow *C = get_chat_window (M->chat_id_);
  if (!C) {
    return;
  }

  C->add_message (std::move (M_));
  C->redraw ();
}

void CursesClient::update_handler (td::td_api::updateMessageSendSucceeded &update) {
  auto M_ = std::move (update.message_);
  auto M = static_cast<td::td_api::message *>(M_.get ());

  TdChatWindow *C = get_chat_window (M->chat_id_);
  if (!C) {
    return;
  }

  C->delete_message (update.old_message_id_);
  C->add_message (std::move (M_));
}

void CursesClient::update_handler (td::td_api::updateMessageSendFailed &update) {
}

void CursesClient::update_handler (td::td_api::updateMessageContent &update) {
  TdChatWindow *C = get_chat_window (update.chat_id_);
  if (!C) {
    return;
  }

  C->update_message_content (update.message_id_, std::move (update.new_content_));
}

void CursesClient::update_handler (td::td_api::updateMessageEdited &update) {
  TdChatWindow *C = get_chat_window (update.chat_id_);
  if (!C) {
    return;
  }

  C->update_message_edited (update.message_id_, update.edit_date_, std::move (update.reply_markup_));
}

void CursesClient::update_handler (td::td_api::updateMessageViews &update) {
  TdChatWindow *C = get_chat_window (update.chat_id_);
  if (!C) {
    return;
  }

  C->update_message_views (update.message_id_, update.views_);
}

void CursesClient::update_handler (td::td_api::updateChat &update) {
  auto chat = static_cast<const td::td_api::chat *>(update.chat_.get ());

  long long chat_id = chat->id_;
  long long order = chat->order_;
    
  cid2chat_[chat->id_] = std::move (update.chat_);

  DialogWindow->updateChat (chat_id, order);
  
  TdChatWindow *C = get_chat_window (chat_id);
  if (C) {
    C->redraw ();
  }
}

void CursesClient::update_handler (td::td_api::updateChatTopMessage &update) {
  auto it = cid2chat_.find (update.chat_id_);
  if (it == cid2chat_.end ()) {
    return;
  }
  it->second->top_message_ = std::move (update.top_message_);
  DialogWindow->redraw ();
}

void CursesClient::update_handler (td::td_api::updateChatOrder &update) {
  auto it = cid2chat_.find (update.chat_id_);
  if (it == cid2chat_.end ()) {
    return;
  }
  it->second->order_ = update.order_;
  DialogWindow->updateChat (update.chat_id_, update.order_);
}

void CursesClient::update_handler (td::td_api::updateChatTitle &update) {
  auto it = cid2chat_.find (update.chat_id_);
  if (it == cid2chat_.end ()) {
    return;
  }
  it->second->title_ = update.title_;
  DialogWindow->redraw ();
  if (ChatWindow) {
    ChatWindow->redraw ();
  }
}

void CursesClient::update_handler (td::td_api::updateChatPhoto &update) {
  auto it = cid2chat_.find (update.chat_id_);
  if (it == cid2chat_.end ()) {
    return;
  }
  it->second->photo_ = std::move (update.photo_);
}

void CursesClient::update_handler (td::td_api::updateChatReadInbox &update) {
  auto it = cid2chat_.find (update.chat_id_);
  if (it == cid2chat_.end ()) {
    return;
  }
  it->second->last_read_inbox_message_id_ = update.last_read_inbox_message_id_;
  it->second->unread_count_ = update.unread_count_;

  DialogWindow->redraw ();
  
  TdChatWindow *C = get_chat_window (update.chat_id_);
  if (C) {
    C->redraw ();
  }
}

void CursesClient::update_handler (td::td_api::updateChatReadOutbox &update) {
  auto it = cid2chat_.find (update.chat_id_);
  if (it == cid2chat_.end ()) {
    return;
  }
  it->second->last_read_outbox_message_id_ = update.last_read_outbox_message_id_;
  
  TdChatWindow *C = get_chat_window (update.chat_id_);
  if (C) {
    C->redraw ();
  }
}

void CursesClient::update_handler (td::td_api::updateChatReplyMarkup &update) {
  auto it = cid2chat_.find (update.chat_id_);
  if (it == cid2chat_.end ()) {
    return;
  }
  it->second->reply_markup_message_id_ = update.reply_markup_message_id_;
  
  TdChatWindow *C = get_chat_window (update.chat_id_);
  if (C) {
    C->redraw ();
  }
}

void CursesClient::update_handler (td::td_api::updateChatDraftMessage &update) {
  auto it = cid2chat_.find (update.chat_id_);
  if (it == cid2chat_.end ()) {
    return;
  }
  it->second->draft_message_ = std::move (update.draft_message_);

  DialogWindow->redraw ();
  
  TdChatWindow *C = get_chat_window (update.chat_id_);
  if (C) {
    C->redraw ();
  }
}

void CursesClient::update_handler (td::td_api::updateNotificationSettings &update) {
}

void CursesClient::update_handler (td::td_api::updateDeleteMessages &update) {
  TdChatWindow *C = get_chat_window (update.chat_id_);
  if (C) {
    for (size_t i = 0; i < update.message_ids_.size (); i++) {
      C->delete_message (update.message_ids_[i]);
    }
  }
}

void CursesClient::update_handler (td::td_api::updateUserAction &update) {
  TdChatWindow *C = get_chat_window (update.chat_id_);
  if (C) {
    C->update_user_action (update.user_id_, update.action_);
  }
}

void CursesClient::update_handler (td::td_api::updateUserStatus &update) {
  auto it = uid2user_.find (update.user_id_);
  if (it == uid2user_.end ()) {
    return;
  }

  it->second->status_ = std::move (update.status_);

  DialogWindow->redraw ();
}

void CursesClient::update_handler (td::td_api::updateUser &update) {
  auto user = static_cast<td::td_api::user *>(update.user_.get ());
  int user_id = user->id_;
  uid2user_[user_id] = std::move (update.user_);
  
  DialogWindow->redraw ();
  if (ChatWindow) {
    ChatWindow->redraw ();
  }
}

void CursesClient::update_handler (td::td_api::updateGroup &update) {
  auto group = static_cast<td::td_api::group *>(update.group_.get ());
  int group_id = group->id_;
  gid2group_[group_id] = std::move (update.group_);
  
  DialogWindow->redraw ();
  if (ChatWindow) {
    ChatWindow->redraw ();
  }
}

void CursesClient::update_handler (td::td_api::updateChannel &update) {
  auto channel = static_cast<td::td_api::channel *>(update.channel_.get ());
  int channel_id = channel->id_;
  chid2channel_[channel_id] = std::move (update.channel_);
  
  DialogWindow->redraw ();
  if (ChatWindow) {
    ChatWindow->redraw ();
  }
}

void CursesClient::update_handler (td::td_api::updateSecretChat &update) {
  auto secret_chat = static_cast<td::td_api::secretChat *>(update.secret_chat_.get ());
  int secret_chat_id = secret_chat->id_;
  scid2secret_chat_[secret_chat_id] = std::move (update.secret_chat_);
  
  DialogWindow->redraw ();
  if (ChatWindow) {
    ChatWindow->redraw ();
  }
}

void CursesClient::update_handler (td::td_api::updateChannelFull &update) {
}

void CursesClient::update_handler (td::td_api::updateUserBlocked &update) {
}

void CursesClient::update_handler (td::td_api::updateServiceNotification &update) {
}

void CursesClient::update_handler (td::td_api::updateFileProgress &update) {
}

void CursesClient::update_handler (td::td_api::updateFile &update) {
}

void CursesClient::update_handler (td::td_api::updatePrivacy &update) {
}

void CursesClient::update_handler (td::td_api::updateOption &update) {
  if (update.name_ == "my_id") {
    auto U = static_cast<const td::td_api::optionInteger *>(update.value_.get ());
    my_id = U->value_;
  }
}

void CursesClient::update_handler (td::td_api::updateStickers &update) {
}

void CursesClient::update_handler (td::td_api::updateSavedAnimations &update) {
}

void CursesClient::update_handler (td::td_api::updateNewInlineQuery &update) {
}

void CursesClient::update_handler (td::td_api::updateNewChosenInlineResult &update) {
}

void CursesClient::update_handler (td::td_api::updateNewCallbackQuery &update) {
}

void CursesClient::update_handler (td::td_api::updateNewInlineCallbackQuery &update) {
}



void CursesClient::on_update (td::tl::tl_object_storage<td::td_api::Update> update) {
  update.get ()->call ([&](auto &object) { update_handler (object); });
}

void main_loop() {
  td::FileLog file_log;
  file_log.init("curses.log.txt");
  td::log_interface = &file_log;

  td::ConcurrentScheduler scheduler;
  scheduler.init(4);

  scheduler.create_actor_unsafe<CursesClient>(0, "CursesClient", false).release();

  scheduler.start();
  while (scheduler.run_main(100)) {
  }
  scheduler.finish();
}

void compute_window_sizes (void) {
  int lines = Root->lines ();
  int cols = Root->cols ();

  int l;
  if (lines >= 20) {
    l = 10;
  } else {
    l = 0;
  }
   
  LogWindow = new TdLogWindow (l, cols, lines - l, 0);

  int r = cols / 2;
  if (r >= 20) {
    r = 20;
  }
  
  //ChatWindow = new TdChatWindow (0, lines - l, cols - r, 0, r);
  DialogWindow = new TdDialogWindow (lines - l, r, 0, 0);
}

int main (int args, char *argv[]) {
  setlocale (LC_ALL, "");

  Root = new RootWindow ();
  tk = termkey_new (0, TERMKEY_FLAG_NOTERMIOS | TERMKEY_FLAG_CONVERTKP);
  assert (tk);
  termkey_start (tk);
  assert (termkey_is_started (tk));
 
  compute_window_sizes ();
 
  main_loop ();

  termkey_stop (tk);
  endwin ();

  return 0;
}
