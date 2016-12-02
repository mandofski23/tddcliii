#pragma once

#include <string>
#include <map>
#include <climits>

#include "auto/tl/td_api.h"

#include "td-window.hpp"
#include "td-line-edit.hpp"

class TdChatWindow : public TdWindow {
  private:
    long long chat_id_;

    int first_allowed_message_id_ = INT_MAX;
   
    bool query_running_ = false;    

    std::map<int,td::tl::tl_object_storage<td::td_api::message>> messages_;

    int bottom_message_id_ = 0;
    int bottom_message_line_num_ = 0;

    TdLineEdit line_edit;
  public:
    TdChatWindow(long long chat_id, int nlines, int ncols, int begin_y, int begin_x);
    ~TdChatWindow () override;

    void key_pressed (const TermKeyKey *key) override;
    void fn_key_pressed (const TermKeyKey *key) override;
    void sym_key_pressed (const TermKeyKey *key) override;
    void redraw () override;
    long long getChatId () {
      return chat_id_;
    }
    void add_message (td::tl::tl_object_storage<td::td_api::message> message);
    void update_message_content (int message_id, td::tl::tl_object_storage<td::td_api::MessageContent> content);
    void update_message_edited (int message_id, int edit_date, td::tl::tl_object_storage<td::td_api::ReplyMarkup> reply_markup);
    void update_message_views (int message_id, int views);
    void update_user_action (int user_id, const td::tl::tl_object_storage<td::td_api::SendMessageAction> &action);
    void delete_message (int message_id);
    void add_history (td::tl::tl_object_storage<td::td_api::messages> messages, int first_allowed_message_id);
    void get_history_failed (int first_allowed_message_id);
};
