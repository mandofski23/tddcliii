#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
//format time:
#include <time.h>
#include <errno.h>

#include "tdc/tdlib-c-bindings.h"
#include "interface.h"
#include "extension.h"

extern struct tdlib_state *TLS;


void tdcb_pack_user (struct tdcb_methods *T, struct tdl_user *U);
void tdcb_pack_group (struct tdcb_methods *T, struct tdl_group *U);
void tdcb_pack_channel (struct tdcb_methods *T, struct tdl_channel *U);
void tdcb_pack_secret_chat (struct tdcb_methods *T, struct tdl_secret_chat *U);

void tdcb_pack_message_id (struct tdcb_methods *T, long long chat_id, int message_id) {
  char s[100];
  sprintf (s, "chat#id%lld@%d ", chat_id, message_id);
  T->pack_string (s);
}

#define tdcb_field(XX,YY) T->YY; T->new_field (XX);
#define tdcb_arr_field(XX,YY) T->YY; T->new_arr_field (XX);

void tdcb_pack_forward_info (struct tdcb_methods *T, struct tdl_message_forward_info *F) {
  T->new_table ();
  if (F->user_id) {
    tdcb_field ("user_id", pack_long (F->user_id));
  }
  if (F->chat_id) {
    tdcb_field ("chat_id", pack_long (F->chat_id));
  }
  if (F->date) {
    tdcb_field ("date", pack_long (F->date));
  }
  if (F->msg_id) {
    tdcb_field ("msg_id", pack_long (F->msg_id));
  }
}

void tdcb_pack_file (struct tdcb_methods *T, struct tdl_file *F) {
  T->new_table ();

  tdcb_field ("id", pack_long (F->id));
  tdcb_field ("size", pack_long (F->size));
  
  if (F->path) {
    tdcb_field ("path", pack_string (F->path));
  }
}

void tdcb_pack_photo_size (struct tdcb_methods *T, struct tdl_photo_size *F) {
  T->new_table ();
  if (F->type) {
    tdcb_field ("type", pack_string (F->type));
  }
  tdcb_pack_file (T, F->file);
  T->new_field ("file");
  tdcb_field ("width", pack_long (F->width));
  tdcb_field ("height", pack_long (F->height));
}

void tdcb_pack_entity (struct tdcb_methods *T, union tdl_message_entity *E) {
  T->new_table ();
  switch (E->type) {
  case tdl_message_entity_mention:
    tdcb_field ("type", pack_string ("mention"));
    tdcb_field ("offset", pack_long (E->simple.offset));
    tdcb_field ("length", pack_long (E->simple.length));
    break;
  case tdl_message_entity_hashtag:
    tdcb_field ("type", pack_string ("hashtag"));
    tdcb_field ("offset", pack_long (E->simple.offset));
    tdcb_field ("length", pack_long (E->simple.length));
    break;
  case tdl_message_entity_bot_command:
    tdcb_field ("type", pack_string ("bot_command"));
    tdcb_field ("offset", pack_long (E->simple.offset));
    tdcb_field ("length", pack_long (E->simple.length));
    break;
  case tdl_message_entity_url:
    tdcb_field ("type", pack_string ("url"));
    tdcb_field ("offset", pack_long (E->simple.offset));
    tdcb_field ("length", pack_long (E->simple.length));
    break;
  case tdl_message_entity_email:
    tdcb_field ("type", pack_string ("email"));
    tdcb_field ("offset", pack_long (E->simple.offset));
    tdcb_field ("length", pack_long (E->simple.length));
    break;
  case tdl_message_entity_bold:
    tdcb_field ("type", pack_string ("bold"));
    tdcb_field ("offset", pack_long (E->simple.offset));
    tdcb_field ("length", pack_long (E->simple.length));
    break;
  case tdl_message_entity_italic:
    tdcb_field ("type", pack_string ("italic"));
    tdcb_field ("offset", pack_long (E->simple.offset));
    tdcb_field ("length", pack_long (E->simple.length));
    break;
  case tdl_message_entity_code:
    tdcb_field ("type", pack_string ("code"));
    tdcb_field ("offset", pack_long (E->simple.offset));
    tdcb_field ("length", pack_long (E->simple.length));
    break;
  case tdl_message_entity_pre:
    tdcb_field ("type", pack_string ("pre"));
    tdcb_field ("offset", pack_long (E->simple.offset));
    tdcb_field ("length", pack_long (E->simple.length));
    break;
  case tdl_message_entity_name:
    tdcb_field ("type", pack_string ("name"));
    tdcb_field ("offset", pack_long (E->name.offset));
    tdcb_field ("length", pack_long (E->name.length));
    tdcb_field ("user_id", pack_long (E->name.user_id));
    break;
  case tdl_message_entity_pre_code:
    tdcb_field ("type", pack_string ("pre_code"));
    tdcb_field ("offset", pack_long (E->pre_code.offset));
    tdcb_field ("length", pack_long (E->pre_code.length));
    tdcb_field ("user_id", pack_string (E->pre_code.language));
    break;
  case tdl_message_entity_text_url:
    tdcb_field ("type", pack_string ("text_url"));
    tdcb_field ("offset", pack_long (E->text_url.offset));
    tdcb_field ("length", pack_long (E->text_url.length));
    tdcb_field ("user_id", pack_string (E->text_url.url));
    break;
  }
}

void tdcb_pack_web_page (struct tdcb_methods *T, struct tdl_web_page *W);

void tdcb_pack_content_text (struct tdcb_methods *T, struct tdl_message_content_text *C) {
  T->new_table ();

  tdcb_field ("type", pack_string ("text"));
  tdcb_field ("text", pack_string (C->text));

  if (C->web_page) {  
    tdcb_pack_web_page (T, C->web_page);
    T->new_field ("web_page");
  }

  if (C->entities_cnt) {
    T->new_array ();
    int i;
    for (i = 0; i < C->entities_cnt; i++) {
      tdcb_pack_entity (T, C->entities[i]);
      T->new_arr_field (i);
    }
    T->new_field ("entities");
  }
}

void tdcb_pack_media_animation (struct tdcb_methods *T, struct tdl_animation *M) {
  T->new_table ();
 
  tdcb_field ("type", pack_string ("animation"));
  tdcb_pack_file (T, M->file);
  T->new_field ("file");
  
  if (M->mime_type) {
    tdcb_field ("mime_type", pack_string (M->mime_type));
  }
  if (M->file_name) {
    tdcb_field ("file_name", pack_string (M->file_name));
  }
  if (M->thumb) {
    tdcb_pack_photo_size (T, M->thumb);
    T->new_field ("thumb");
  }

  tdcb_field ("width", pack_long (M->width));
  tdcb_field ("height", pack_long (M->height));
}

void tdcb_pack_media_audio (struct tdcb_methods *T, struct tdl_audio *M) {
  T->new_table ();
 
  tdcb_field ("type", pack_string ("audio"));
  tdcb_pack_file (T, M->file);
  T->new_field ("file");
  
  if (M->mime_type) {
    tdcb_field ("mime_type", pack_string (M->mime_type));
  }
  if (M->file_name) {
    tdcb_field ("file_name", pack_string (M->file_name));
  }
  if (M->title) {
    tdcb_field ("title", pack_string (M->title));
  }
  if (M->performer) {
    tdcb_field ("performer", pack_string (M->performer));
  }
  tdcb_field ("duration", pack_long (M->duration));
  
  if (M->album_cover_thumb) {
    tdcb_pack_photo_size (T, M->album_cover_thumb);
    T->new_field ("album_cover_thumb");
  }
}

void tdcb_pack_media_document (struct tdcb_methods *T, struct tdl_document *M) {
  T->new_table ();
  
  tdcb_field ("type", pack_string ("document"));
  tdcb_pack_file (T, M->file);
  T->new_field ("file");
  
  if (M->mime_type) {
    tdcb_field ("mime_type", pack_string (M->mime_type));
  }
  if (M->file_name) {
    tdcb_field ("file_name", pack_string (M->file_name));
  }
  if (M->thumb) {
    tdcb_pack_photo_size (T, M->thumb);
    T->new_field ("thumb");
  }
}

void tdcb_pack_media_photo (struct tdcb_methods *T, struct tdl_photo *M) {
  T->new_table ();
  
  tdcb_field ("type", pack_string ("photo"));
  tdcb_field ("id", pack_long (M->id));

  T->new_array ();

  int i;
  for (i = 0; i < M->sizes_cnt; i++) {
    tdcb_pack_photo_size (T, M->sizes[i]);
    T->new_arr_field (i);
  }
  T->new_field ("sizes");
}

void tdcb_pack_media_sticker (struct tdcb_methods *T, struct tdl_sticker *M) {
  T->new_table ();
  
  tdcb_field ("type", pack_string ("sticker"));
  tdcb_pack_file (T, M->file);
  T->new_field ("file");
  
  if (M->emoji) {
    tdcb_field ("emoji", pack_string (M->emoji));
  }
  if (M->thumb) {
    tdcb_pack_photo_size (T, M->thumb);
    T->new_field ("thumb");
  }

  tdcb_field ("set_id", pack_long (M->set_id));
  tdcb_field ("height", pack_long (M->height));
  tdcb_field ("width", pack_long (M->width));
  tdcb_field ("rating", pack_double (M->rating));
}

void tdcb_pack_media_video (struct tdcb_methods *T, struct tdl_video *M) {
  T->new_table ();

  tdcb_field ("type", pack_string ("video"));
  tdcb_pack_file (T, M->file);
  T->new_field ("file");
  
  if (M->mime_type) {
    tdcb_field ("mime_type", pack_string (M->mime_type));
  }
  if (M->file_name) {
    tdcb_field ("file_name", pack_string (M->file_name));
  }
  if (M->thumb) {
    tdcb_pack_photo_size (T, M->thumb);
    T->new_field ("thumb");
  }
  
  tdcb_field ("duration", pack_long (M->duration));
  tdcb_field ("height", pack_long (M->height));
  tdcb_field ("width", pack_long (M->width));
}

void tdcb_pack_media_voice (struct tdcb_methods *T, struct tdl_voice *M) {
  T->new_table ();

  tdcb_field ("type", pack_string ("voice"));
  tdcb_pack_file (T, M->file);
  T->new_field ("file");
  
  if (M->mime_type) {
    tdcb_field ("mime_type", pack_string (M->mime_type));
  }
  if (M->file_name) {
    tdcb_field ("file_name", pack_string (M->file_name));
  }
  if (M->thumb) {
    tdcb_pack_photo_size (T, M->thumb);
    T->new_field ("thumb");
  }
  
  tdcb_field ("duration", pack_long (M->duration));
}

void tdcb_pack_web_page (struct tdcb_methods *T, struct tdl_web_page *W) {
  T->new_table ();
  if (W->url) {
    tdcb_field ("url", pack_string (W->url));
  }
  if (W->display_url) {
    tdcb_field ("display_url", pack_string (W->display_url));
  }
  if (W->web_page_type) {
    tdcb_field ("web_page_type", pack_string (W->web_page_type));
  }
  if (W->site_name) {
    tdcb_field ("site_name", pack_string (W->site_name));
  }
  if (W->title) {
    tdcb_field ("title", pack_string (W->title));
  }
  if (W->description) {
    tdcb_field ("description", pack_string (W->description));
  }
  if (W->embed_url) {
    tdcb_field ("embed_url", pack_string (W->embed_url));
  }
  if (W->embed_mime_type) {
    tdcb_field ("embed_mime_type", pack_string (W->embed_mime_type));
  }
  if (W->embed_width) {
    tdcb_field ("embed_width", pack_long (W->embed_width));
  }
  if (W->embed_height) {
    tdcb_field ("embed_height", pack_long (W->embed_height));
  }
  if (W->duration) {
    tdcb_field ("duration", pack_long (W->duration));
  }
  if (W->author) {
    tdcb_field ("author", pack_string (W->author));
  }
  if (W->animation) {
    tdcb_pack_media_animation (T, W->animation);
    T->new_field ("animation");
  }
  if (W->photo) {
    tdcb_pack_media_photo (T, W->photo);
    T->new_field ("photo");
  }
  if (W->document) {
    tdcb_pack_media_document (T, W->document);
    T->new_field ("document");
  }
  if (W->sticker) {
    tdcb_pack_media_sticker (T, W->sticker);
    T->new_field ("sticker");
  }
}

void tdcb_pack_media (struct tdcb_methods *T, union tdl_message_media *M) {
  switch (M->type) {
  case tdl_media_animation:
    return tdcb_pack_media_animation (T, &M->animation);
  case tdl_media_audio:
    return tdcb_pack_media_audio (T, &M->audio);
  case tdl_media_document:
    return tdcb_pack_media_document (T, &M->document);
  case tdl_media_photo:
    return tdcb_pack_media_photo (T, &M->photo);
  case tdl_media_sticker:
    return tdcb_pack_media_sticker (T, &M->sticker);
  case tdl_media_video:
    return tdcb_pack_media_video (T, &M->video);
  case tdl_media_voice:
    return tdcb_pack_media_voice (T, &M->voice);
  default:
    assert (0);
  }
}

void tdcb_pack_content_media (struct tdcb_methods *T, struct tdl_message_content_media *C) {
  T->new_table ();
  tdcb_field ("type", pack_string ("media"));
  
  if (C->caption) {
    tdcb_field ("caption", pack_string (C->caption));
  }
  
  tdcb_field ("is_watched", pack_bool (C->is_watched));
  tdcb_pack_media (T, C->media);
  T->new_field ("media");
}

void tdcb_pack_content_venue (struct tdcb_methods *T, struct tdl_message_content_venue *C) {
  T->new_table ();

  tdcb_field ("type", pack_string ("venue"));
  tdcb_field ("longitude", pack_double (C->longitude));
  tdcb_field ("latitude", pack_double (C->latitude));
  
  if (C->title) {
    tdcb_field ("title", pack_string (C->title));
  }
  if (C->address) {
    tdcb_field ("address", pack_string (C->address));
  }
  if (C->provider) {
    tdcb_field ("provider", pack_string (C->provider));
  }
  if (C->venue_id) {
    tdcb_field ("venue_id", pack_string (C->venue_id));
  }
}

void tdcb_pack_content_contact (struct tdcb_methods *T, struct tdl_message_content_contact *C) {
  T->new_table ();
  
  tdcb_field ("type", pack_string ("contact"));
  if (C->first_name) {
    tdcb_field ("first_name", pack_string (C->first_name));
  }
  if (C->last_name) {
    tdcb_field ("last_name", pack_string (C->last_name));
  }
  if (C->phone) {  
    tdcb_field ("phone", pack_string (C->phone));
  }
  if (C->user_id) {
    tdcb_field ("user_id", pack_long (C->user_id));
  }
}

void tdcb_pack_action_group_create (struct tdcb_methods *T, struct tdl_message_action_group_create *A) {
  T->new_table ();
  
  tdcb_field ("type", pack_string ("group_create"));
  tdcb_field ("title", pack_string (A->title));

  T->new_array ();
  int i;
  for (i = 0; i < A->members_cnt; i++) {
    tdcb_pack_user (T, A->members[i]);
    T->new_arr_field (i);
  }
  T->new_field ("members");
}

void tdcb_pack_action_channel_create (struct tdcb_methods *T, struct tdl_message_action_channel_create *A) {
  T->new_table ();
  tdcb_field ("type", pack_string ("channel_create"));
  tdcb_field ("title", pack_string (A->title));
}

void tdcb_pack_action_chat_change_title (struct tdcb_methods *T, struct tdl_message_action_chat_change_title *A) {
  T->new_table ();
  tdcb_field ("type", pack_string ("chat_change_title"));
  tdcb_field ("title", pack_string (A->title));
}

void tdcb_pack_action_chat_change_photo (struct tdcb_methods *T, struct tdl_message_action_chat_change_photo *A) {
  T->new_table ();
  tdcb_field ("type", pack_string ("chat_change_photo"));
}

void tdcb_pack_action_chat_delete_photo (struct tdcb_methods *T, struct tdl_message_action_chat_delete_photo *A) {
  T->new_table ();
  tdcb_field ("type", pack_string ("chat_delete_photo"));
}

void tdcb_pack_action_chat_add_members (struct tdcb_methods *T, struct tdl_message_action_chat_add_members *A) {
  T->new_table ();
  tdcb_field ("type", pack_string ("chat_add_members"));

  T->new_array ();
  int i;
  for (i = 0; i < A->members_cnt; i++) {
    tdcb_pack_user (T, A->members[i]);
    T->new_arr_field (i);
  }
  T->new_field ("members");
}

void tdcb_pack_action_chat_join_by_link (struct tdcb_methods *T, struct tdl_message_action_chat_join_by_link *A) {
  T->new_table ();
  tdcb_field ("type", pack_string ("chat_join_by_link"));
  tdcb_field ("inviter_user_id", pack_long (A->inviter_user_id));
}

void tdcb_pack_action_chat_delete_member (struct tdcb_methods *T, struct tdl_message_action_chat_delete_member *A) {
  T->new_table ();
  tdcb_field ("type", pack_string ("chat_delete_member"));
  tdcb_pack_user (T, A->user);
  T->new_field ("member");
}

void tdcb_pack_action_chat_migrate_to (struct tdcb_methods *T, struct tdl_message_action_chat_migrate_to *A) {
  T->new_table ();
  tdcb_field ("type", pack_string ("chat_migrate_to"));
  tdcb_field ("channel_id", pack_long (A->channel_id));
}

void tdcb_pack_action_chat_migrate_from (struct tdcb_methods *T, struct tdl_message_action_chat_migrate_from *A) {
  T->new_table ();
  tdcb_field ("type", pack_string ("chat_migrate_from"));
  tdcb_field ("group_id", pack_long (A->group_id));
  tdcb_field ("group_title", pack_string (A->title));
}

void tdcb_pack_action_pin_message (struct tdcb_methods *T, struct tdl_message_action_pin_message *A) {
  T->new_table ();
  tdcb_field ("type", pack_string ("pin_message"));
  tdcb_field ("message", pack_long (A->message_id));
}

void tdcb_pack_content_action (struct tdcb_methods *T, union tdl_message_action *C) {
  T->new_table ();
  tdcb_field ("type", pack_string ("action"));
  
  switch (C->action) {
  case tdl_message_action_type_group_create:
    tdcb_pack_action_group_create (T, &C->group_create);
    break;
  case tdl_message_action_type_channel_create:
    tdcb_pack_action_channel_create (T, &C->channel_create);
    break;
  case tdl_message_action_type_chat_change_title:
    tdcb_pack_action_chat_change_title (T, &C->change_title);
    break;
  case tdl_message_action_type_chat_change_photo:
    tdcb_pack_action_chat_change_photo (T, &C->change_photo);
    break;
  case tdl_message_action_type_chat_delete_photo:
    tdcb_pack_action_chat_delete_photo (T, &C->delete_photo);
    break;
  case tdl_message_action_type_chat_add_members:
    tdcb_pack_action_chat_add_members (T, &C->add_members);
    break;
  case tdl_message_action_type_chat_join_by_link:
    tdcb_pack_action_chat_join_by_link (T, &C->join_by_link);
    break;
  case tdl_message_action_type_chat_delete_member:
    tdcb_pack_action_chat_delete_member (T, &C->delete_member);
    break;
  case tdl_message_action_type_chat_migrate_to:
    tdcb_pack_action_chat_migrate_to (T, &C->migrate_to);
    break;
  case tdl_message_action_type_chat_migrate_from:
    tdcb_pack_action_chat_migrate_from (T, &C->migrate_from);
    break;
  case tdl_message_action_type_pin_message:
    tdcb_pack_action_pin_message (T, &C->pin_message);
    break;
  }
  T->new_field ("action");
}

void tdcb_pack_content_deleted (struct tdcb_methods *T, struct tdl_message_content_deleted *C) {
  T->new_table ();
  tdcb_field ("type", pack_string ("deleted"));
}

void tdcb_pack_content_unsupported (struct tdcb_methods *T, struct tdl_message_content_unsupported *C) {
  T->new_table ();
  tdcb_field ("type", pack_string ("unsupported"));
}

void tdcb_pack_content (struct tdcb_methods *T, union tdl_message_content *C) {
  switch (C->type) {
  case tdl_message_content_type_text:
    return tdcb_pack_content_text (T, &C->text);
  case tdl_message_content_type_media:
    return tdcb_pack_content_media (T, &C->media);
  case tdl_message_content_type_venue:
    return tdcb_pack_content_venue (T, &C->venue);
  case tdl_message_content_type_contact:
    return tdcb_pack_content_contact (T, &C->contact);
  case tdl_message_content_type_action:
    return tdcb_pack_content_action (T, &C->action);
  case tdl_message_content_type_deleted:
    return tdcb_pack_content_deleted (T, &C->deleted);
  case tdl_message_content_type_unsupported:
    return tdcb_pack_content_unsupported (T, &C->unsupported);    
  default:
    assert (0);
  }
}

void tdcb_pack_reply_markup (struct tdcb_methods *T, union tdl_reply_markup *M) {
  T->new_table ();

  if (M->type == tdl_reply_markup_type_hide_keyboard) {
    tdcb_field ("type", pack_string ("hide"));
    tdcb_field ("personal", pack_bool (M->hide.personal));
    return;
  }
  if (M->type == tdl_reply_markup_type_force_reply) {
    tdcb_field ("type", pack_string ("force_reply"));
    tdcb_field ("personal", pack_bool (M->force_reply.personal));
    return;
  }
  assert (M->type == tdl_reply_markup_type_show_keyboard);
  tdcb_field ("type", pack_string ("show"));
  tdcb_field ("personal", pack_bool (M->show.personal));
  tdcb_field ("one_time", pack_bool (M->show.one_time));
  tdcb_field ("resize", pack_bool (M->show.resize_keyboard));

  T->new_array ();
  int i;
  int p = 0;
  for (i = 0; i < M->show.rows_cnt; i++) {
    int j;
    T->new_array ();
    for (j = 0; j < M->show.rows[i]; j++) {
      struct tdl_button *B = M->show.buttons[p ++];
      T->new_array ();
      switch (B->type) {
      case tdl_button_type_text:
        tdcb_field ("type", pack_string ("text"));
        break;
      case tdl_button_type_request_phone_number:
        tdcb_field ("type", pack_string ("request_phone_number"));
        break;
      case tdl_button_type_request_location:
        tdcb_field ("type", pack_string ("request_location"));
        break;
      }
      if (B->text) {
        tdcb_field ("text", pack_string (B->text));
      }
      T->new_arr_field (j);
    }
    T->new_arr_field (i);
  }
  T->new_field ("buttons");
}

void tdcb_pack_message (struct tdcb_methods *T, struct tdl_message *M) {
  T->new_table ();
  
  struct tdl_chat_info *C = tdlib_instant_get_chat (TLS, M->chat_id);
  assert (C);
  tdcb_pack_message_id (T, C->id, M->id);
  T->new_field ("id");
  
  tdcb_field ("chat_id", pack_long (M->chat_id));
  
  if (M->sender_user_id) {
    tdcb_field ("sender_user_id", pack_long (M->sender_user_id));
  }
  tdcb_field ("can_be_deleted", pack_bool (M->can_be_deleted));
  tdcb_field ("is_post", pack_bool (M->is_post));
  tdcb_field ("date", pack_long (M->date));
  tdcb_field ("edit_date", pack_long (M->edit_date));
  if (M->forward_info) {
    tdcb_pack_forward_info (T, M->forward_info);
    T->new_field ("forward_info");
  }
  if (M->reply_to_message_id) {
    tdcb_field ("reply_to_message_id", pack_long (M->reply_to_message_id));
  }
  if (M->via_bot_user_id) {
    tdcb_field ("via_bot_user_id", pack_long (M->via_bot_user_id));
  }
  if (M->views) {
    tdcb_field ("views", pack_long (M->views));
  }

  tdcb_pack_content (T, M->content);
  T->new_field ("content");
  
  if (M->reply_markup && M->reply_markup->type != tdl_reply_markup_type_none) {
    tdcb_pack_reply_markup (T, M->reply_markup);
    T->new_field ("reply_markup");
  }
}

void tdcb_pack_bot_info (struct tdcb_methods *T, struct tdl_bot_info *B) {
  T->new_table ();
  if (B->description) {
    tdcb_field ("description", pack_string (B->description));
  }
  if (B->commands_cnt) {
    T->new_array ();
    int i;
    for (i = 0; i < B->commands_cnt; i++) {
      T->new_table ();
      tdcb_field ("command", pack_string (B->commands[i]->command));
      tdcb_field ("description", pack_string (B->commands[i]->description));
      T->new_arr_field (i);
    }
  
    T->new_field ("commands");
  }
}

void tdcb_pack_chat_member (struct tdcb_methods *T, struct tdl_chat_member *M) {
  T->new_table ();
  tdcb_field ("user_id", pack_long (M->user_id));
  tdcb_field ("inviter_user_id", pack_long (M->inviter_user_id));
  tdcb_field ("join_date", pack_long (M->join_date));
  switch (M->role) {
  case tdl_chat_member_role_creator:
    tdcb_field ("role", pack_string ("creator"));
    break;
  case tdl_chat_member_role_editor:
    tdcb_field ("role", pack_string ("editor"));
    break;
  case tdl_chat_member_role_moderator:
    tdcb_field ("role", pack_string ("moderator"));
    break;
  case tdl_chat_member_role_general:
    tdcb_field ("role", pack_string ("general"));
    break;
  case tdl_chat_member_role_left:
    tdcb_field ("role", pack_string ("left"));
    break;
  case tdl_chat_member_role_kicked:
    tdcb_field ("role", pack_string ("kicked"));
    break;
  }
  if (M->bot_info) {
    tdcb_pack_bot_info (T, M->bot_info);
    T->new_field ("bot_info");
  }
}

void tdcb_pack_chat (struct tdcb_methods *T, struct tdl_chat_info *C) {
  T->new_table ();

  tdcb_field ("id", pack_long (C->id));
  tdcb_field ("title", pack_string (C->title));
  
  if (C->photo) {
    T->new_table ();
    
    if (C->photo->big) {
      tdcb_pack_file (T, C->photo->big);
      T->new_field ("big");
    }
    if (C->photo->small) {
      tdcb_pack_file (T, C->photo->small);
      T->new_field ("small");
    }
    T->new_field ("photo");
  }

  tdcb_field ("order", pack_long (C->order));
  tdcb_field ("unread_count", pack_long (C->unread_count));
  tdcb_field ("last_read_inbox_message_id", pack_long (C->last_read_inbox_message_id));
  tdcb_field ("last_read_outbox_message_id", pack_long (C->last_read_outbox_message_id));
  tdcb_field ("reply_markup_message_id", pack_long (C->reply_markup_message_id));
  
  if (C->notification_settings) {
    T->new_table ();
    tdcb_field ("mute_for", pack_long (C->notification_settings->mute_for));
    tdcb_field ("sound", pack_string (C->notification_settings->sound));
    tdcb_field ("show_preview", pack_bool (C->notification_settings->show_preview));
    T->new_field ("notification_settings");
  }

  switch (C->chat->type) {
  case tdl_chat_type_user:
    tdcb_pack_user (T, &C->chat->user);
    break;
  case tdl_chat_type_group:
    tdcb_pack_group (T, &C->chat->group);
    break;
  case tdl_chat_type_channel:
    tdcb_pack_channel (T, &C->chat->channel);
    break;
  case tdl_chat_type_secret_chat:
    tdcb_pack_secret_chat (T, &C->chat->secret_chat);
    break;
  }
  
  T->new_field ("type");
}

void tdcb_pack_link_state (struct tdcb_methods *T, enum tdl_user_link_state link) {
  switch (link) {
  case tdl_user_link_state_knows_phone_number:
    return T->pack_string ("knowns_phone");
  case tdl_user_link_state_contact:
    return T->pack_string ("contact");
  default:
    return T->pack_string ("none");
  }
}

void tdcb_pack_user (struct tdcb_methods *T, struct tdl_user *U) {
  T->new_table ();
  
  tdcb_field ("type", pack_string ("user"));
  tdcb_field ("id", pack_long (U->id));
  
  if (U->first_name) {
    tdcb_field ("first_name", pack_string (U->first_name));
  }
  if (U->last_name) {
    tdcb_field ("last_name", pack_string (U->last_name));
  }
  if (U->username) {
    tdcb_field ("username", pack_string (U->username));
  }
  if (U->phone_number) {
    tdcb_field ("phone_number", pack_string (U->phone_number));
  }
  if (U->status && U->status->type != tdl_user_status_empty) {
    switch (U->status->type) {
    case tdl_user_status_empty:
      break;
    case tdl_user_status_online:
      tdcb_field ("status", pack_string ("online"));
      tdcb_field ("expires", pack_long (U->status->when));
      break;
    case tdl_user_status_offline:
      tdcb_field ("status", pack_string ("offline"));
      tdcb_field ("last_seen", pack_long (U->status->when));
      break;
    case tdl_user_status_recently:
      tdcb_field ("status", pack_string ("recently"));
      break;
    case tdl_user_status_last_week:
      tdcb_field ("status", pack_string ("last_week"));
      break;
    case tdl_user_status_last_month:
      tdcb_field ("status", pack_string ("last_month"));
      break;
    }
  }
  if (U->photo) {
    T->new_table ();
    tdcb_field ("id", pack_long (U->photo->id));
    
    if (U->photo->big) {
      tdcb_pack_file (T, U->photo->big);
      T->new_field ("big");
    }
    if (U->photo->small) {
      tdcb_pack_file (T, U->photo->small);
      T->new_field ("small");
    }

    T->new_field ("photo");
  }

  tdcb_pack_link_state (T, U->my_link);
  T->new_field ("my_link");
  
  tdcb_pack_link_state (T, U->foreign_link);
  T->new_field ("foreign_link");

  tdcb_field ("is_verified", pack_bool (U->is_verified));
  
  if (U->restriction_reason) {
    tdcb_field ("restriction_reason", pack_string (U->restriction_reason));
  }

  tdcb_field ("have_access", pack_bool (U->have_access));
  tdcb_field ("deleted", pack_bool (U->deleted));

  if (U->bot_type) {
    T->new_table ();
    tdcb_field ("can_join_group_chats", pack_bool (U->bot_type->can_join_group_chats));
    tdcb_field ("can_read_all_group_chat_messages", pack_bool (U->bot_type->can_read_all_group_chat_messages));
    tdcb_field ("is_inline", pack_bool (U->bot_type->is_inline));
    if (U->bot_type->inline_query_placeholder) {
      tdcb_field ("inline_query_placeholder", pack_string (U->bot_type->inline_query_placeholder));
    }
    T->new_field ("bot_type");
  }

  if (U->full && !U->full->need_update) {
    tdcb_field ("is_blocked", pack_bool (U->full->is_blocked));
    if (U->full->about) {
      tdcb_field ("about", pack_string (U->full->about));
    }
    if (U->full->bot_info) {
      tdcb_pack_bot_info (T, U->full->bot_info);
      T->new_field ("bot_info");
    }
  }
}

void tdcb_pack_group (struct tdcb_methods *T, struct tdl_group *G) {
  T->new_table ();
  
  tdcb_field ("type", pack_string ("group"));
  tdcb_field ("id", pack_long (G->id));
  tdcb_field ("members_count", pack_long (G->members_count));
  
  switch (G->role) {
  case tdl_chat_member_role_creator:
    tdcb_field ("role", pack_string ("creator"));
    break;
  case tdl_chat_member_role_editor:
    tdcb_field ("role", pack_string ("editor"));    
    break;
  case tdl_chat_member_role_moderator:
    tdcb_field ("role", pack_string ("moderator"));    
    break;
  case tdl_chat_member_role_general:
    tdcb_field ("role", pack_string ("general"));    
    break;
  case tdl_chat_member_role_left:
    tdcb_field ("role", pack_string ("left"));    
    break;
  case tdl_chat_member_role_kicked:
    tdcb_field ("role", pack_string ("kicked"));    
    break;
  }

  tdcb_field ("anyone_can_edit", pack_bool (G->anyone_can_edit));
  tdcb_field ("is_active", pack_bool (G->is_active));
  tdcb_field ("migrated_to_channel_id", pack_long (G->migrated_to_channel_id));

  if (G->full && !G->full->need_update) {
    if (G->full->invite_link) {
      tdcb_field ("invite_link", pack_string (G->full->invite_link));
    }
      
    tdcb_field ("creator_user_id", pack_long (G->full->creator_user_id));

    T->new_array ();
    int i;
    for (i = 0; i < G->full->members_cnt; i++) {
      tdcb_pack_chat_member (T, G->full->members[i]);
      T->new_arr_field (i);
    }
    T->new_field ("members");
  }
}

void tdcb_pack_channel (struct tdcb_methods *T, struct tdl_channel *Ch) {
  T->new_table ();
  
  tdcb_field ("type", pack_string ("channel"));
  tdcb_field ("id", pack_long (Ch->id));
  
  if (Ch->username) {
    tdcb_field ("username", pack_string (Ch->username));
  }
  if (Ch->date) {
    tdcb_field ("date", pack_long (Ch->date));
  }
  switch (Ch->role) {
  case tdl_chat_member_role_creator:
    tdcb_field ("role", pack_string ("creator"));
    break;
  case tdl_chat_member_role_editor:
    tdcb_field ("role", pack_string ("editor"));    
    break;
  case tdl_chat_member_role_moderator:
    tdcb_field ("role", pack_string ("moderator"));    
    break;
  case tdl_chat_member_role_general:
    tdcb_field ("role", pack_string ("general"));    
    break;
  case tdl_chat_member_role_left:
    tdcb_field ("role", pack_string ("left"));    
    break;
  case tdl_chat_member_role_kicked:
    tdcb_field ("role", pack_string ("kicked"));    
    break;
  }

  tdcb_field ("anyone_can_invite", pack_bool (Ch->anyone_can_invite));
  tdcb_field ("sign_messages", pack_bool (Ch->sign_messages));
  tdcb_field ("is_broadcast", pack_bool (Ch->is_broadcast));
  tdcb_field ("is_supergroup", pack_bool (Ch->is_supergroup));
  tdcb_field ("is_verified", pack_bool (Ch->is_verified));

  if (Ch->restriction_reason) {
    tdcb_field ("restriction_reason", pack_string (Ch->restriction_reason));
  }
  
  if (Ch->full && !Ch->full->need_update ) {
    if (Ch->full->about) {
      tdcb_field ("about", pack_string (Ch->full->about));
    }
    tdcb_field ("members_count", pack_long (Ch->full->members_cnt));
    tdcb_field ("admins_count", pack_long (Ch->full->admins_cnt));
    tdcb_field ("kicked_count", pack_long (Ch->full->kicked_cnt));
    tdcb_field ("can_get_members", pack_bool (Ch->full->can_get_members));
    tdcb_field ("can_set_username", pack_bool (Ch->full->can_set_username));
    if (Ch->full->invite_link) {
      tdcb_field ("invite_link", pack_string (Ch->full->invite_link));
    }
    if (Ch->full->migrated_from_group_id) {
      tdcb_field ("migrated_from_group_id", pack_long (Ch->full->migrated_from_group_id));
      tdcb_field ("migrated_from_max_message_id", pack_long (Ch->full->migrated_from_max_message_id));
    }
  }
}

void tdcb_pack_secret_chat (struct tdcb_methods *T, struct tdl_secret_chat *S) {
  T->new_table ();
  tdcb_field ("type", pack_string ("secret_chat"));
  tdcb_field ("id", pack_long (S->id));
}

void tdcb_pack_res_arg (struct tdcb_methods *T, struct res_arg *A, struct result_argument_desc *D) {
  int op = D->type & 127;
  switch (op) {
    case ra_user:
      return tdcb_pack_user (T, A->user);
    case ra_group:
      return tdcb_pack_group (T, A->group);
    case ra_channel:
      return tdcb_pack_channel (T, A->channel);
    case ra_secret_chat:
      return tdcb_pack_secret_chat (T, A->secret_chat);
    case ra_peer:
      switch (A->peer->type) {
        case tdl_chat_type_user:
          return tdcb_pack_user (T, A->user);
        case tdl_chat_type_group:
          return tdcb_pack_group (T, A->group);
        case tdl_chat_type_channel:
          return tdcb_pack_channel (T, A->channel);
        case tdl_chat_type_secret_chat:
          return tdcb_pack_secret_chat (T, A->secret_chat);
      }
      break;
    case ra_chat:
      return tdcb_pack_chat (T, A->chat);
    case ra_message:
      return tdcb_pack_message (T, A->message);
    case ra_int:
      return T->pack_long (A->num);
    case ra_chat_member:
      return tdcb_pack_chat_member (T, A->chat_member);
    case ra_string:
      return T->pack_string (A->str);
    case ra_photo:
      return tdcb_pack_media_photo (T, A->photo);
    case ra_invite_link_info:
    case ra_none:
    default:
      return T->pack_bool (0);
  }
}

void tdcb_universal_pack_answer (struct tdcb_methods *T, struct in_command *cmd, int success, struct res_arg *args) {
  T->new_table ();
  if (cmd->query_id) {
    tdcb_field ("query_id", pack_long (cmd->query_id));
  }

  if (!success) {
    tdcb_field ("result", pack_string ("FAIL"));
    tdcb_field ("error_code", pack_long (TLS->error_code));
    tdcb_field ("error", pack_string (TLS->error));
    return;
  }

  T->new_table ();

  struct command *C = cmd->cmd;

  int i;
  for (i = 0; i < 10; i++) {
    if (!C->res_args[i].name) {
      break;
    }

    if (C->res_args[i].type & ra_vector) {
      T->new_array ();

      int j;
      for (j = 0; j < args[i].vec_len; j++) {
        tdcb_pack_res_arg (T, &args[i].vec[j], &C->res_args[i]);
        T->new_arr_field (j);
      }
    } else {
      tdcb_pack_res_arg (T, &args[i], &C->res_args[i]);
    }
    T->new_field (C->res_args[i].name);
  }

  T->new_field ("result"); 
}


void tdcb_universal_pack_update (struct tdcb_methods *T, struct update_description *D, struct res_arg args[]) {
  T->new_table ();

  tdcb_field ("update_type", pack_string (D->name));

  T->new_table ();

  int i;
  for (i = 0; i < 10; i++) {  
    if (!D->res_args[i].name) {
      break;
    }

    if (D->res_args[i].type & ra_vector) {
      T->new_array ();

      int j;
      for (j = 0; j < args[i].vec_len; j++) {
        tdcb_pack_res_arg (T, &args[i].vec[j], &D->res_args[i]);
        T->new_arr_field (j);
      }
    } else {
      tdcb_pack_res_arg (T, &args[i], &D->res_args[i]);
    }
    T->new_field (D->res_args[i].name);
  }

  T->new_field ("update"); 
}

int tdcb_parse_argument_modifier (struct tdcb_methods *T, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!T->is_string ()) {
    return -1;
  }
  
  char *s = T->get_string ();

  if (!s || strlen (s) < 2 || s[0] != '[' || s[strlen (s) - 1] != ']') {
    free (s);
    A->str = NULL;
    return -1;
  } else {
    A->str = s;
    A->flags = 1;
    return 0;
  }
}

int tdcb_parse_argument_string (struct tdcb_methods *T, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!T->is_string ()) {
    return -1;
  }

  A->str = T->get_string ();
  A->flags = 1;
  return 0;
}

int tdcb_parse_argument_long (struct tdcb_methods *T, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!T->is_long ()) {
    A->num = NOT_FOUND;
    return -1;
  }
  
  A->num = T->get_long ();
  return 0;
}

int tdcb_parse_argument_double (struct tdcb_methods *T, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!T->is_double ()) {
    A->dval = NOT_FOUND;
    return -1;
  }
  
  A->dval = T->get_double ();
  return 0;
}

tdl_message_id_t cur_token_msg_id (char *s, struct in_command *cmd);
int tdcb_parse_argument_msg_id (struct tdcb_methods *T, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!T->is_string ()) {
    A->msg_id.message_id = -1;
    return -1;
  }

  char *s = T->get_string ();
  A->msg_id = cur_token_msg_id (s, NULL);
  free (s);

  return A->msg_id.message_id < 0 ? -1 : 0;
}

struct tdl_chat_info *cur_token_peer (char *s, int mode, struct in_command *cmd);
int tdcb_parse_argument_chat (struct tdcb_methods *T, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!T->is_string ()) {
    return -1;
  }

  char *s = T->get_string ();

  int op = D->type & 255;
    
  int m = -1;
  if (op == ca_user) { m = tdl_chat_type_user; }
  if (op == ca_group) { m = tdl_chat_type_group; }
  if (op == ca_channel) { m = tdl_chat_type_channel; }
  if (op == ca_secret_chat) { m = tdl_chat_type_secret_chat; }            

  A->chat = cur_token_peer (s, m, NULL);
  free (s);

  return A->chat ? 0 : -1;
}

int tdcb_parse_argument_any (struct tdcb_methods *T, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  int op = D->type & 255;

  switch (op) {
  case ca_user:
  case ca_group:
  case ca_secret_chat:
  case ca_channel:
  case ca_chat:
    return tdcb_parse_argument_chat (T, cmd, A, D);
  case ca_file_name:
  case ca_string:
  case ca_media_type:
  case ca_command:
  case ca_file_name_end:
  case ca_string_end:
  case ca_msg_string_end:
    return tdcb_parse_argument_string (T, cmd, A, D);
  case ca_modifier:
    return tdcb_parse_argument_modifier (T, cmd, A, D);
  case ca_number:
    return tdcb_parse_argument_long (T, cmd, A, D);
  case ca_double:
    return tdcb_parse_argument_double (T, cmd, A, D);
  case ca_msg_id:
    return tdcb_parse_argument_msg_id (T, cmd, A, D);
  case ca_none:
  default:
    logprintf ("type=%d\n", op);
    assert (0);
    return -1;
  }
}

int tdcb_parse_argument_period (struct tdcb_methods *T, struct in_command *cmd, struct arg *A, struct command_argument_desc *D) {
  if (!T->is_array ()) {
    return -1;
  }
  A->flags = 2;

  A->vec_len = 0;
  A->vec = malloc (0);

  struct arg AT;
  int idx = 0;
  for (idx = 0;;idx ++) {
    T->get_arr_field (idx);

    if (T->is_nil ()) {
      T->pop ();
      break;
    }
    
    int r = tdcb_parse_argument_any (T, cmd, &AT, D);
    T->pop ();
    if (r == -1) {
      return -1;
    }
    
    A->vec = realloc (A->vec, sizeof (struct arg) * (A->vec_len + 1));
    A->vec[A->vec_len ++] = AT;
  }
  
  return 0;
}

void free_argument (struct arg *A);
void free_args_list (struct arg args[], int cnt);

extern struct command_argument_desc carg_0;
extern struct command_argument_desc carg_1;
extern struct command commands[];

int tdcb_parse_command_line (struct tdcb_methods *T, struct arg args[], struct in_command *cmd) {
  //struct command_argument_desc *D = command->args;
  //void (*fun)(struct command *, int, struct arg[], struct in_command *) = command->fun;
  struct command *command = NULL;

  int p = 0;  
  int ok = 0;

  while (1) {
    struct command_argument_desc *D;
    if (p == 0) {
      D = &carg_0;
    } else if (p == 1) {
      D = &carg_1;
    } else {
      D = &command->args[p - 2];
    }
    if (!D->type) {
      break;
    }

    T->get_field (D->name);

    if (T->is_nil ()) {
      if (!(D->type & ca_optional)) {
        fail_interface (TLS, cmd, ENOSYS, "can not parse arg '%s'", D->name);
        ok = -1;
      } else {
        if (D->type & ca_period) {
          tdcb_parse_argument_period (T, cmd, &args[p], D);
        } else {
          tdcb_parse_argument_any (T, cmd, &args[p], D);
        }
      }
    } else {
      int r;
      if (D->type & ca_period) {
        r = tdcb_parse_argument_period (T, cmd, &args[p], D);
      } else {
        r = tdcb_parse_argument_any (T, cmd, &args[p], D);
      }
      if (r < 0) {
        fail_interface (TLS, cmd, ENOSYS, "can not parse arg '%s'", D->name);
        ok = r;
      }
    }

    T->pop ();
    if (ok < 0) { break; }

    if (p == 1) {
      command = commands;      
      while (command->name) {
        if (!strcmp (command->name, args[p].str)) {
          break;
        }
        command ++;
      }
      if (!command->name) {
        fail_interface (TLS, cmd, ENOSYS, "unknown command %s", args[p].str);
        ok = -1;
        break;
      }
    }

    p ++;
  }

  return ok;
}

void tdcb_run_command (struct tdcb_methods *T, struct in_command *cmd) {  
  struct arg args[12];
  memset (&args, 0, sizeof (args));
  
  int res = tdcb_parse_command_line (T, args, cmd);

  if (!res) {
    struct command *command = commands;    

    while (command->name) {
      if (!strcmp (command->name, args[1].str)) {
        break;
      }
      command ++;
    }

    cmd->query_id = (int)find_modifier (args[0].vec_len, args[0].vec, "id", 2);
    int count = (int)find_modifier (args[0].vec_len, args[0].vec, "x", 1);
    if (!count) { count = 1; }
    cmd->cmd = command;

    command->fun (command, 12, args, cmd);
  }
  
  free_args_list (args, 12);
}
