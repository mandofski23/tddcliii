#pragma once

#include <list>
#include <set>
#include <unordered_map>

#include "ncursesw/ncurses.h"
#include "ncursesw/panel.h"

#include "td/utils/tl_storer.h"

#include "td-window.hpp"
#include "td-curses-utils.hpp"

class TdQueryCallback {
  public:
    virtual void on_result(td::tl::tl_object_storage<td::td_api::nullary_object> result) = 0;    
    virtual void on_error(td::tl::tl_object_storage<td::td_api::error> error) = 0;
    virtual ~TdQueryCallback() {
    }
};

class CursesClient final : public td::Actor {
 public:
  explicit CursesClient(bool is_test_dc) : is_test_dc_(is_test_dc) {
  }

  class TdAuthStateCallback : public TdQueryCallback {
    void on_result (td::tl::tl_object_storage<td::td_api::nullary_object> result) override {
      instance_->authentificate_continue (td::tl::move_as<td::td_api::AuthState>(result));
    }
    void on_error (td::tl::tl_object_storage<td::td_api::error> error) override {
      td_curses_log ("AUTH_ERROR: " + error.get ()->message_);
      instance_->authentificate_restart ();
    }
  };
  
  void send_request(td::tl::tl_object_storage<td::td_api::function> f, std::unique_ptr<TdQueryCallback> handler) {
    auto id = handlers_.create(std::move(handler));
    if (!td_.empty()) {
      send_closure(td_, &td::ClientActor::request, id, std::move(f));
    } else {
      LOG(ERROR) << "Failed to send: " << td::tl::to_string(f);
    }
  };
  
  static CursesClient *instance_;
  const td::td_api::chat *get_chat (long long chat_id);
  const td::td_api::user *get_user (int user_id);
  const td::td_api::group *get_group (int group_id);
  const td::td_api::channel *get_channel (int channel_id);
  const td::td_api::secretChat *get_secret_chat (int secret_chat_id);
  int my_id;

 private:
  std::unordered_map<long long, td::tl::tl_object_storage<td::td_api::chat>> cid2chat_;
  std::unordered_map<int, td::tl::tl_object_storage<td::td_api::user>> uid2user_;
  std::unordered_map<int, td::tl::tl_object_storage<td::td_api::group>> gid2group_;
  std::unordered_map<int, td::tl::tl_object_storage<td::td_api::channel>> chid2channel_;
  std::unordered_map<int, td::tl::tl_object_storage<td::td_api::secretChat>> scid2secret_chat_;
  void authentificate_restart ();
  void authentificate_continue (td::tl::tl_object_storage<td::td_api::AuthState> result);

  void update_handler (td::td_api::updateNewMessage &update);
  void update_handler (td::td_api::updateMessageSendSucceeded &update);
  void update_handler (td::td_api::updateMessageSendFailed &update);
  void update_handler (td::td_api::updateMessageContent &update);
  void update_handler (td::td_api::updateMessageEdited &update);
  void update_handler (td::td_api::updateMessageViews &update);
  void update_handler (td::td_api::updateChat &update);
  void update_handler (td::td_api::updateChatTopMessage &update);
  void update_handler (td::td_api::updateChatOrder &update);
  void update_handler (td::td_api::updateChatTitle &update);
  void update_handler (td::td_api::updateChatPhoto &update);
  void update_handler (td::td_api::updateChatReadInbox &update);
  void update_handler (td::td_api::updateChatReadOutbox &update);
  void update_handler (td::td_api::updateChatReplyMarkup &update);
  void update_handler (td::td_api::updateChatDraftMessage &update);
  void update_handler (td::td_api::updateNotificationSettings &update);
  void update_handler (td::td_api::updateDeleteMessages &update);
  void update_handler (td::td_api::updateUserAction &update);
  void update_handler (td::td_api::updateUserStatus &update);
  void update_handler (td::td_api::updateUser &update);
  void update_handler (td::td_api::updateGroup &update);
  void update_handler (td::td_api::updateChannel &update);
  void update_handler (td::td_api::updateSecretChat &update);
  void update_handler (td::td_api::updateChannelFull &update);
  void update_handler (td::td_api::updateUserBlocked &update);
  void update_handler (td::td_api::updateServiceNotification &update);
  void update_handler (td::td_api::updateFileProgress &update);
  void update_handler (td::td_api::updateFile &update);
  void update_handler (td::td_api::updatePrivacy &update);
  void update_handler (td::td_api::updateOption &update);
  void update_handler (td::td_api::updateStickers &update);
  void update_handler (td::td_api::updateSavedAnimations &update);
  void update_handler (td::td_api::updateNewInlineQuery &update);
  void update_handler (td::td_api::updateNewChosenInlineResult &update);
  void update_handler (td::td_api::updateNewCallbackQuery &update);
  void update_handler (td::td_api::updateNewInlineCallbackQuery &update);

  void start_up() override {
    yield();
  }
  
  void on_update (td::tl::tl_object_storage<td::td_api::Update> update);
  void on_result (td::uint64 id, td::tl::tl_object_storage<td::td_api::nullary_object> result);
  void on_error (td::uint64 id, td::tl::tl_object_storage<td::td_api::error> error);


  void on_before_close() {
    td_.reset();
  }

  void on_closed() {
    LOG(INFO) << "on_closed";
    if (close_flag_) {
      ready_to_stop_ = true;
      yield();
      return;
    }
    init_td();
  }

  void run_stdin() {
  }
  

  std::unique_ptr<td::ClientActor::Callback> make_td_callback();
  
  void init_td();

  void init() {
    instance_ = this;
    init_td();

    stdin_ = td::Fd::Stdin().clone();
    CHECK(stdin_.is_ref());
    td::FdRef(stdin_).set_observer(this);
    subscribe(stdin_, td::Fd::Read);

    authentificate_restart (); 
  }


  bool inited_ = false;
  void loop() override;


  void timeout_expired() override {
  }

  /*void add_cmd(std::string cmd) {
    cmd_queue_.push(cmd);
  }*/

  //td::int32 my_id_ = 0;

  bool is_test_dc_ = false;
  td::ActorOwn<td::ClientActor> td_;
  //std::queue<std::string> cmd_queue_;
  bool close_flag_ = false;
  bool ready_to_stop_ = false;
  td::Fd stdin_;
  td::Container<std::unique_ptr<TdQueryCallback>> handlers_;
  int32_t auth_state_;
};
