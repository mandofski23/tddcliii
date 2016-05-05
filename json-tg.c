#ifdef USE_JSON

#include <jansson.h>
#include "tdc/tdlib-c-bindings.h"
#include "json-tg.h"
#include "interface.h"
#include <assert.h>
//format time:
#include <time.h>

#ifndef json_boolean
#define json_boolean(val)      ((val) ? json_true() : json_false())
#endif

extern struct tdlib_state *TLS;

json_t *json_pack_message_id (enum tdl_chat_type type, int chat_id, int message_id) {
  char s[100];
  switch (type) {
    case tdl_chat_type_user:
      sprintf (s, "user#id%d@%d ", chat_id, message_id);
      break;
    case tdl_chat_type_group:
      sprintf (s, "group#id%d@%d ", chat_id, message_id);
      break;
    case tdl_chat_type_channel:
      sprintf (s, "channel#id%d@%d ", chat_id, message_id);
      break;
    case tdl_chat_type_secret_chat:
      sprintf (s, "secret_chat#id%d@%d ", chat_id, message_id);
      break;
    default:
      assert (0);
  }
  return json_string (s);
}

json_t *json_pack_forward_info (struct tdl_message_forward_info *F) {
  json_t *res = json_object ();
  if (F->user_id) {
    json_object_set (res, "user_id", json_integer (F->user_id));
  }
  if (F->chat_id) {
    json_object_set (res, "chat_id", json_integer (F->chat_id));
  }
  if (F->date) {
    json_object_set (res, "date", json_integer (F->date));
  }
  if (F->msg_id) {
    json_object_set (res, "msg_id", json_integer (F->msg_id));
  }
  return res;
}

json_t *json_pack_file (struct tdl_file *F) {
  json_t *res = json_object ();
  json_object_set (res, "id", json_integer (F->id));
  json_object_set (res, "size", json_integer (F->size));
  if (F->path) {
    json_object_set (res, "path", json_string (F->path));
  }

  return res;
}

json_t *json_pack_photo_size (struct tdl_photo_size *F) {
  json_t *res = json_object ();
  if (F->type) {
    json_object_set (res, "type", json_string (F->type));
  }
  json_object_set (res, "file", json_pack_file (F->file));
  json_object_set (res, "width", json_integer (F->width));
  json_object_set (res, "height", json_integer (F->height));

  return res;
}

json_t *json_pack_entity (union tdl_message_entity *E) {
  json_t *res = json_object ();
  switch (E->type) {
  case tdl_message_entity_mention:
    json_object_set (res, "type", json_string ("mention"));
    json_object_set (res, "offset", json_integer (E->simple.offset));
    json_object_set (res, "length", json_integer (E->simple.length));
    break;
  case tdl_message_entity_hashtag:
    json_object_set (res, "type", json_string ("hashtag"));
    json_object_set (res, "offset", json_integer (E->simple.offset));
    json_object_set (res, "length", json_integer (E->simple.length));
    break;
  case tdl_message_entity_bot_command:
    json_object_set (res, "type", json_string ("bot_command"));
    json_object_set (res, "offset", json_integer (E->simple.offset));
    json_object_set (res, "length", json_integer (E->simple.length));
    break;
  case tdl_message_entity_url:
    json_object_set (res, "type", json_string ("url"));
    json_object_set (res, "offset", json_integer (E->simple.offset));
    json_object_set (res, "length", json_integer (E->simple.length));
    break;
  case tdl_message_entity_email:
    json_object_set (res, "type", json_string ("email"));
    json_object_set (res, "offset", json_integer (E->simple.offset));
    json_object_set (res, "length", json_integer (E->simple.length));
    break;
  case tdl_message_entity_bold:
    json_object_set (res, "type", json_string ("bold"));
    json_object_set (res, "offset", json_integer (E->simple.offset));
    json_object_set (res, "length", json_integer (E->simple.length));
    break;
  case tdl_message_entity_italic:
    json_object_set (res, "type", json_string ("italic"));
    json_object_set (res, "offset", json_integer (E->simple.offset));
    json_object_set (res, "length", json_integer (E->simple.length));
    break;
  case tdl_message_entity_code:
    json_object_set (res, "type", json_string ("code"));
    json_object_set (res, "offset", json_integer (E->simple.offset));
    json_object_set (res, "length", json_integer (E->simple.length));
    break;
  case tdl_message_entity_pre:
    json_object_set (res, "type", json_string ("pre"));
    json_object_set (res, "offset", json_integer (E->simple.offset));
    json_object_set (res, "length", json_integer (E->simple.length));
    break;
  case tdl_message_entity_pre_code:
    json_object_set (res, "type", json_string ("pre_code"));
    json_object_set (res, "offset", json_integer (E->pre_code.offset));
    json_object_set (res, "length", json_integer (E->pre_code.length));
    json_object_set (res, "language", json_string (E->pre_code.language));
    break;
  case tdl_message_entity_text_url:
    json_object_set (res, "type", json_string ("text_url"));
    json_object_set (res, "offset", json_integer (E->text_url.offset));
    json_object_set (res, "length", json_integer (E->text_url.length));
    json_object_set (res, "url", json_string (E->text_url.url));
    break;
  }
  return res;
}

json_t *json_pack_web_page (struct tdl_web_page *W);
json_t *json_pack_content_text (struct tdl_message_content_text *C) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("text"));
  
  json_object_set (res, "text", json_string (C->text));

  if (C->web_page) {  
    json_object_set (res, "web_page", json_pack_web_page (C->web_page));
  }

  if (C->entities_cnt) {
    json_t *arr = json_array ();
    int i;
    for (i = 0; i < C->entities_cnt; i++) {
      json_array_append (arr, json_pack_entity (C->entities[i]));
    }
    json_object_set (res, "entities", arr);
  }

  return res;
}

json_t *json_pack_media_animation (struct tdl_animation *M) {
  json_t *res = json_object ();
  
  json_object_set (res, "type", json_string ("animation"));
  json_object_set (res, "file", json_pack_file (M->file));
  if (M->mime_type) {
    json_object_set (res, "mime_type", json_string (M->mime_type));
  }
  if (M->file_name) {
    json_object_set (res, "file_name", json_string (M->file_name));
  }
  if (M->thumb) {
    json_object_set (res, "thumb", json_pack_photo_size (M->thumb));
  }
 
  json_object_set (res, "width", json_integer (M->width));
  json_object_set (res, "height", json_integer (M->height));
  
  return res;
}

json_t *json_pack_media_audio (struct tdl_audio *M) {
  json_t *res = json_object ();
  
  json_object_set (res, "type", json_string ("audio"));
  json_object_set (res, "file", json_pack_file (M->file));
  if (M->file_name) {
    json_object_set (res, "file_name", json_string (M->file_name));
  }
  if (M->mime_type) {
    json_object_set (res, "mime_type", json_string (M->mime_type));
  }
  if (M->title) {
    json_object_set (res, "title", json_string (M->title));
  }
  if (M->performer) {
    json_object_set (res, "performer", json_string (M->performer));
  }
  json_object_set (res, "duration", json_integer (M->duration));
  if (M->album_cover_thumb) {
    json_object_set (res, "album_cover_thumb", json_pack_photo_size (M->album_cover_thumb));
  }
  return res;
}

json_t *json_pack_media_document (struct tdl_document *M) {
  json_t *res = json_object ();
  
  json_object_set (res, "type", json_string ("document"));
  json_object_set (res, "file", json_pack_file (M->file));
  if (M->mime_type) {
    json_object_set (res, "mime_type", json_string (M->mime_type));
  }
  if (M->file_name) {
    json_object_set (res, "file_name", json_string (M->file_name));
  }
  if (M->thumb) {
    json_object_set (res, "thumb", json_pack_photo_size (M->thumb));
  }
  
  return res;
}

json_t *json_pack_media_photo (struct tdl_photo *M) {
  json_t *res = json_object ();
  
  json_object_set (res, "type", json_string ("photo"));
  json_object_set (res, "id", json_integer (M->id));

  json_t *arr = json_array ();
  int i;
  for (i = 0; i < M->sizes_cnt; i++) {
    json_array_append (arr, json_pack_photo_size (M->sizes[i]));
  }
  json_object_set (res, "sizes", arr);
  
  return res;
}

json_t *json_pack_media_sticker (struct tdl_sticker *M) {
  json_t *res = json_object ();
  
  json_object_set (res, "type", json_string ("sticker"));
  json_object_set (res, "file", json_pack_file (M->file));
  if (M->emoji) {
    json_object_set (res, "emoji", json_string (M->emoji));
  }
  if (M->thumb) {
    json_object_set (res, "thumb", json_pack_photo_size (M->thumb));
  }
  json_object_set (res, "set_id", json_integer (M->set_id));
  json_object_set (res, "height", json_integer (M->height));
  json_object_set (res, "width", json_integer (M->width));
  json_object_set (res, "rating", json_real (M->rating));
 
  return res;
}

json_t *json_pack_media_video (struct tdl_video *M) {
  json_t *res = json_object ();
  
  json_object_set (res, "type", json_string ("video"));
  json_object_set (res, "file", json_pack_file (M->file));
  if (M->mime_type) {
    json_object_set (res, "mime_type", json_string (M->mime_type));
  }
  if (M->file_name) {
    json_object_set (res, "file_name", json_string (M->file_name));
  }
  if (M->thumb) {
    json_object_set (res, "thumb", json_pack_photo_size (M->thumb));
  }
 
  json_object_set (res, "duration", json_integer (M->duration));
  json_object_set (res, "width", json_integer (M->width));
  json_object_set (res, "height", json_integer (M->height));
  
  return res;
}

json_t *json_pack_media_voice (struct tdl_voice *M) {
  json_t *res = json_object ();
  
  json_object_set (res, "type", json_string ("voice"));
  json_object_set (res, "file", json_pack_file (M->file));
  if (M->mime_type) {
    json_object_set (res, "mime_type", json_string (M->mime_type));
  }
  if (M->file_name) {
    json_object_set (res, "file_name", json_string (M->file_name));
  }
  if (M->thumb) {
    json_object_set (res, "thumb", json_pack_photo_size (M->thumb));
  }
 
  json_object_set (res, "duration", json_integer (M->duration));
  if (M->waveform) {
    json_object_set (res, "waveform", json_stringn ((char *)M->waveform, M->waveform_size));
  }
  
  return res;
}

json_t *json_pack_web_page (struct tdl_web_page *W) {
  json_t *res = json_object ();
  if (W->url) {
    json_object_set (res, "url", json_string (W->url));
  }
  if (W->display_url) {
    json_object_set (res, "display_url", json_string (W->display_url));
  }
  if (W->web_page_type) {
    json_object_set (res, "web_page_type", json_string (W->web_page_type));
  }
  if (W->site_name) {
    json_object_set (res, "site_name", json_string (W->site_name));
  }
  if (W->title) {
    json_object_set (res, "title", json_string (W->title));
  }
  if (W->description) {
    json_object_set (res, "description", json_string (W->description));
  }
  if (W->embed_url) {
    json_object_set (res, "embed_url", json_string (W->embed_url));
  }
  if (W->embed_mime_type) {
    json_object_set (res, "embed_mime_type", json_string (W->embed_mime_type));
  }
  if (W->embed_width) {
    json_object_set (res, "embed_width", json_integer (W->embed_width));
  }
  if (W->embed_height) {
    json_object_set (res, "embed_height", json_integer (W->embed_height));
  }
  if (W->duration) {
    json_object_set (res, "duration", json_integer (W->duration));
  }
  if (W->author) {
    json_object_set (res, "author", json_string (W->author));
  }
  if (W->animation) {
    json_object_set (res, "animation", json_pack_media_animation (W->animation));
  }
  if (W->photo) {
    json_object_set (res, "photo", json_pack_media_photo (W->photo));
  }
  if (W->document) {
    json_object_set (res, "document", json_pack_media_document (W->document));
  }
  if (W->sticker) {
    json_object_set (res, "sticker", json_pack_media_sticker (W->sticker));
  }
  return res;
}

json_t *json_pack_media (union tdl_message_media *M) {
  switch (M->type) {
  case tdl_media_animation:
    return json_pack_media_animation (&M->animation);
  case tdl_media_audio:
    return json_pack_media_audio (&M->audio);
  case tdl_media_document:
    return json_pack_media_document (&M->document);
  case tdl_media_photo:
    return json_pack_media_photo (&M->photo);
  case tdl_media_sticker:
    return json_pack_media_sticker (&M->sticker);
  case tdl_media_video:
    return json_pack_media_video (&M->video);
  case tdl_media_voice:
    return json_pack_media_voice (&M->voice);
  default:
    assert (0);
    return NULL;
  }
}

json_t *json_pack_content_media (struct tdl_message_content_media *C) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("media"));
  
  if (C->caption) {
    json_object_set (res, "caption", json_string (C->caption));
  }
  
  json_object_set (res, "is_watched", json_boolean (C->is_watched));
  json_object_set (res, "media", json_pack_media (C->media));

  return res;
}

json_t *json_pack_content_venue (struct tdl_message_content_venue *C) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("venue"));
  json_object_set (res, "longitude", json_real (C->longitude));
  json_object_set (res, "latitude", json_real (C->latitude));
  if (C->title) {
    json_object_set (res, "title", json_string (C->title));
  }
  if (C->address) {
    json_object_set (res, "address", json_string (C->address));
  }
  if (C->provider) {
    json_object_set (res, "provider", json_string (C->provider));
  }
  if (C->venue_id) {
    json_object_set (res, "venue_id", json_string (C->venue_id));
  }

  return res;
}

json_t *json_pack_content_contact (struct tdl_message_content_contact *C) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("contact"));
  if (C->first_name) {
    json_object_set (res, "first_name", json_string (C->first_name));
  }
  if (C->last_name) {
    json_object_set (res, "last_name", json_string (C->last_name));
  }
  if (C->phone) {  
    json_object_set (res, "phone", json_string (C->phone));
  }
  if (C->user_id) {
    json_object_set (res, "user_id", json_integer (C->user_id));
  }

  return res;
}

json_t *json_pack_action_group_create (struct tdl_message_action_group_create *A) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("group_create"));
  json_object_set (res, "title", json_string (A->title));
  json_t *arr = json_array ();
  int i;
  for (i = 0; i < A->members_cnt; i++) {
    json_array_append (arr, json_pack_user (A->members[i]));
  }
  json_object_set (res, "members", arr);
  return res;
}

json_t *json_pack_action_channel_create (struct tdl_message_action_channel_create *A) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("channel_create"));
  json_object_set (res, "title", json_string (A->title));
  return res;
}

json_t *json_pack_action_chat_change_title (struct tdl_message_action_chat_change_title *A) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("chat_change_title"));
  json_object_set (res, "title", json_string (A->title));
  return res;
}

json_t *json_pack_action_chat_change_photo (struct tdl_message_action_chat_change_photo *A) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("chat_change_photo"));
  //json_object_set (res, "photo", json_string (A->title));
  return res;
}

json_t *json_pack_action_chat_delete_photo (struct tdl_message_action_chat_delete_photo *A) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("chat_delete_photo"));
  return res;
}

json_t *json_pack_action_chat_add_members (struct tdl_message_action_chat_add_members *A) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("chat_add_members"));
  json_t *arr = json_array ();
  int i;
  for (i = 0; i < A->members_cnt; i++) {
    json_array_append (arr, json_pack_user (A->members[i]));
  }
  json_object_set (res, "members", arr);
  return res;
}

json_t *json_pack_action_chat_join_by_link (struct tdl_message_action_chat_join_by_link *A) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("chat_join_by_link"));
  json_object_set (res, "inviter_user_id", json_integer (A->inviter_user_id));
  return res;
}

json_t *json_pack_action_chat_delete_member (struct tdl_message_action_chat_delete_member *A) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("chat_join_by_link"));
  json_object_set (res, "member", json_pack_user (A->user));
  return res;
}

json_t *json_pack_action_chat_migrate_to (struct tdl_message_action_chat_migrate_to *A) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("chat_migrate_to"));
  json_object_set (res, "channel_id", json_integer (A->channel_id));
  return res;
}

json_t *json_pack_action_chat_migrate_from (struct tdl_message_action_chat_migrate_from *A) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("chat_migrate_from"));
  json_object_set (res, "group_id", json_integer (A->group_id));
  json_object_set (res, "group_title", json_string (A->title));
  return res;
}

json_t *json_pack_action_pin_message (struct tdl_message_action_pin_message *A) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("pin_message"));
  json_object_set (res, "message", json_integer (A->message_id));
  return res;
}

json_t *json_pack_content_action (union tdl_message_action *C) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("action"));
  json_t *action = NULL;
  switch (C->action) {
  case tdl_message_action_type_group_create:
    action = json_pack_action_group_create (&C->group_create);
    break;
  case tdl_message_action_type_channel_create:
    action = json_pack_action_channel_create (&C->channel_create);
    break;
  case tdl_message_action_type_chat_change_title:
    action = json_pack_action_chat_change_title (&C->change_title);
    break;
  case tdl_message_action_type_chat_change_photo:
    action = json_pack_action_chat_change_photo (&C->change_photo);
    break;
  case tdl_message_action_type_chat_delete_photo:
    action = json_pack_action_chat_delete_photo (&C->delete_photo);
    break;
  case tdl_message_action_type_chat_add_members:
    action = json_pack_action_chat_add_members (&C->add_members);
    break;
  case tdl_message_action_type_chat_join_by_link:
    action = json_pack_action_chat_join_by_link (&C->join_by_link);
    break;
  case tdl_message_action_type_chat_delete_member:
    action = json_pack_action_chat_delete_member (&C->delete_member);
    break;
  case tdl_message_action_type_chat_migrate_to:
    action = json_pack_action_chat_migrate_to (&C->migrate_to);
    break;
  case tdl_message_action_type_chat_migrate_from:
    action = json_pack_action_chat_migrate_from (&C->migrate_from);
    break;
  case tdl_message_action_type_pin_message:
    action = json_pack_action_pin_message (&C->pin_message);
    break;
  }
  json_object_set (res, "action", action);
  return res;
}

json_t *json_pack_content_deleted (struct tdl_message_content_deleted *C) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("deleted"));
  return res;
}

json_t *json_pack_content_unsupported (struct tdl_message_content_unsupported *C) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("unsupported"));
  return res;
}

json_t *json_pack_content (union tdl_message_content *C) {
  switch (C->type) {
  case tdl_message_content_type_text:
    return json_pack_content_text (&C->text);
  case tdl_message_content_type_media:
    return json_pack_content_media (&C->media);
  case tdl_message_content_type_venue:
    return json_pack_content_venue (&C->venue);
  case tdl_message_content_type_contact:
    return json_pack_content_contact (&C->contact);
  case tdl_message_content_type_action:
    return json_pack_content_action (&C->action);
  case tdl_message_content_type_deleted:
    return json_pack_content_deleted (&C->deleted);
  case tdl_message_content_type_unsupported:
    return json_pack_content_unsupported (&C->unsupported);
  default:
    assert (0);
    return NULL;
  }
}

json_t *json_pack_reply_markup (union tdl_reply_markup *M) {
  json_t *res = json_object ();

  if (M->type == tdl_reply_markup_type_hide_keyboard) {
    json_object_set (res, "type", json_string ("hide"));
    json_object_set (res, "personal", json_boolean (M->hide.personal));
    return res;
  }
  if (M->type == tdl_reply_markup_type_force_reply) {
    json_object_set (res, "type", json_string ("force_reply"));
    json_object_set (res, "personal", json_boolean (M->force_reply.personal));
    return res;
  }
  assert (M->type == tdl_reply_markup_type_show_keyboard);
  json_object_set (res, "type", json_string ("show"));
  json_object_set (res, "personal", json_boolean (M->show.personal));
  json_object_set (res, "show", json_boolean (M->show.one_time));
  json_object_set (res, "resize", json_boolean (M->show.resize_keyboard));
  
  json_t *arr = json_array ();
  int i;
  int p = 0;
  for (i = 0; i < M->show.rows_cnt; i++) {
    int j;
    json_t *row = json_array ();
    for (j = 0; j < M->show.rows[i]; j++) {
      struct tdl_button *B = M->show.buttons[p ++];
      json_t *b = json_object ();
      switch (B->type) {
      case tdl_button_type_text:
        json_object_set (b, "type", json_string ("text"));
        break;
      case tdl_button_type_request_phone_number:
        json_object_set (b, "type", json_string ("request_phone_number"));
        break;
      case tdl_button_type_request_location:
        json_object_set (b, "type", json_string ("request_location"));
        break;
      }
      if (B->text) {
        json_object_set (b, "text", json_string (B->text));
      }
      json_array_append (row, b);
    }
    json_array_append (arr, row);
  }
  json_object_set (res, "buttons", arr);
  return res;
}

json_t *json_pack_message (struct tdl_message *M) {
  json_t *res = json_object ();
  struct tdl_chat_info *C = tdlib_instant_get_chat (TLS, M->chat_id);
  assert (C);
  json_object_set (res, "id", json_pack_message_id (C->chat->type, C->chat->id, M->id));
  if (M->sender_user_id) {
    json_object_set (res, "sender_user_id", json_integer (M->sender_user_id));
  }
  json_object_set (res, "can_be_deleted", json_boolean (M->can_be_deleted));
  json_object_set (res, "is_post", json_boolean (M->is_post));
  json_object_set (res, "date", json_integer (M->date));
  json_object_set (res, "edit_date", json_integer (M->edit_date));
  if (M->forward_info) {
    json_object_set (res, "forward_info", json_pack_forward_info (M->forward_info));
  }
  if (M->reply_to_message_id) {
    json_object_set (res, "reply_to_message_id", json_integer (M->reply_to_message_id));
  }
  if (M->via_bot_user_id) {
    json_object_set (res, "via_bot_user_id", json_integer (M->via_bot_user_id));
  }
  if (M->views) {
    json_object_set (res, "views", json_integer (M->views));
  }
 
  json_object_set (res, "content", json_pack_content (M->content));
  if (M->reply_markup && M->reply_markup->type != tdl_reply_markup_type_none) {
    json_object_set (res, "reply_markup", json_pack_reply_markup (M->reply_markup));
  }
  return res;
}

json_t *json_pack_bot_info (struct tdl_bot_info *B) {
  json_t *res = json_object ();
  if (B->description) {
    json_object_set (res, "description", json_string (B->description));
  }
  if (B->commands_cnt) {
    json_t *arr = json_array ();
    int i;
    for (i = 0; i < B->commands_cnt; i++) {
      json_t *a = json_object ();
      json_object_set (a, "command", json_string (B->commands[i]->command));
      json_object_set (a, "description", json_string (B->commands[i]->description));
    }
    json_object_set (res, "commands", arr);
  }
  return res;
}

json_t *json_pack_chat_member (struct tdl_chat_member *M) {
  json_t *res = json_object ();
  json_object_set (res, "user", json_pack_user (M->user));
  json_object_set (res, "inviter_user_id", json_integer (M->inviter_user_id));
  json_object_set (res, "join_date", json_integer (M->join_date));
  switch (M->role) {
  case tdl_chat_member_role_creator:
    json_object_set (res, "role", json_string ("creator"));
    break;
  case tdl_chat_member_role_editor:
    json_object_set (res, "role", json_string ("editor"));
    break;
  case tdl_chat_member_role_moderator:
    json_object_set (res, "role", json_string ("moderator"));
    break;
  case tdl_chat_member_role_general:
    json_object_set (res, "role", json_string ("general"));
    break;
  case tdl_chat_member_role_left:
    json_object_set (res, "role", json_string ("left"));
    break;
  case tdl_chat_member_role_kicked:
    json_object_set (res, "role", json_string ("kicked"));
    break;
  }
  if (M->bot_info) {
    json_object_set (res, "bot_info", json_pack_bot_info (M->bot_info));
  }
  return res;
}

json_t *json_pack_chat (struct tdl_chat_info *C) {
  json_t *res = json_object ();

  json_object_set (res, "id", json_integer (C->id));
  json_object_set (res, "title", json_string (C->title));
  if (C->photo) {
    json_t *a = json_object ();
    if (C->photo->big) {
      json_object_set (a, "big", json_pack_file (C->photo->big));
    }
    if (C->photo->small) {
      json_object_set (a, "small", json_pack_file (C->photo->small));
    }
    json_object_set (res, "photo", a);
  }
  json_object_set (res, "order", json_integer (C->order));
  json_object_set (res, "unread_count", json_integer (C->unread_count));
  json_object_set (res, "last_read_inbox_message_id", json_integer (C->last_read_inbox_message_id));
  json_object_set (res, "last_read_outbox_message_id", json_integer (C->last_read_outbox_message_id));
  json_object_set (res, "reply_markup_message_id", json_integer (C->reply_markup_message_id));
  if (C->notification_settings) {
    json_t *a = json_object ();
    json_object_set (a, "mute_for", json_integer (C->notification_settings->mute_for));
    json_object_set (a, "sound", json_string (C->notification_settings->sound)); 
    json_object_set (a, "show_preview", json_integer (C->notification_settings->show_preview));
    json_object_set (res, "notification_settings", a);
  }

  switch (C->chat->type) {
  case tdl_chat_type_user:
    json_object_set (res, "type", json_pack_user (&C->chat->user));
    break;
  case tdl_chat_type_group:
    json_object_set (res, "type", json_pack_group (&C->chat->group));
    break;
  case tdl_chat_type_channel:
    json_object_set (res, "type", json_pack_channel (&C->chat->channel));
    break;
  case tdl_chat_type_secret_chat:
    json_object_set (res, "type", json_pack_secret_chat (&C->chat->secret_chat));
    break;
  }
  return res;
}

json_t *json_pack_link_state (enum tdl_user_link_state link) {
  switch (link) {
  case tdl_user_link_state_knows_phone_number:
    return json_string ("knows_phone");
  case tdl_user_link_state_contact:
    return json_string ("contact");
  default:
    return json_string ("none");
  }
}

json_t *json_pack_user (struct tdl_user *U) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("user"));
  json_object_set (res, "id", json_integer (U->id));
  if (U->first_name) {
    json_object_set (res, "first_name", json_string (U->first_name));
  }
  if (U->last_name) {
    json_object_set (res, "last_name", json_string (U->last_name));
  }
  if (U->username) {
    json_object_set (res, "username", json_string (U->username));
  }
  if (U->phone_number) {
    json_object_set (res, "phone_number", json_string (U->phone_number));
  }
  if (U->status && U->status->type != tdl_user_status_empty) {
    switch (U->status->type) {
    case tdl_user_status_empty:
      break;
    case tdl_user_status_online:
      json_object_set (res, "status", json_string ("online"));
      json_object_set (res, "expire", json_integer (U->status->when));
      break;
    case tdl_user_status_offline:
      json_object_set (res, "status", json_string ("offline"));
      json_object_set (res, "when", json_integer (U->status->when));
      break;
    case tdl_user_status_recently:
      json_object_set (res, "status", json_string ("recently"));
      break;
    case tdl_user_status_last_week:
      json_object_set (res, "status", json_string ("last_week"));
      break;
    case tdl_user_status_last_month:
      json_object_set (res, "status", json_string ("last_month"));
      break;
    }
  }
  if (U->photo) {
    json_t *a = json_object ();
    json_object_set (a, "id", json_integer (U->photo->id));
    if (U->photo->big) {
      json_object_set (a, "big", json_pack_file (U->photo->big));
    }
    if (U->photo->small) {
      json_object_set (a, "small", json_pack_file (U->photo->small));
    }

    json_object_set (res, "photo", a);
  }
  json_object_set (res, "my_link", json_pack_link_state (U->my_link));
  json_object_set (res, "foreign_link", json_pack_link_state (U->foreign_link));
  json_object_set (res, "is_verified", json_boolean (U->is_verified));
  if (U->restriction_reason) {
    json_object_set (res, "restriction_reason", json_string (U->restriction_reason));
  }
  json_object_set (res, "have_access", json_boolean (U->have_access));
  json_object_set (res, "deleted", json_boolean (U->deleted));

  if (U->bot_type) {
    json_t *a = json_object ();
    json_object_set (a, "can_join_group_chats", json_boolean (U->bot_type->can_join_group_chats));
    json_object_set (a, "can_read_all_group_chat_messages", json_boolean (U->bot_type->can_read_all_group_chat_messages));
    json_object_set (a, "is_inline", json_boolean (U->bot_type->is_inline));
    if (U->bot_type->inline_query_placeholder) {
      json_object_set (a, "inline_query_placeholder", json_string (U->bot_type->inline_query_placeholder));
    }
    json_object_set (res, "bot_type", a);
  }

  if (U->full && !U->full->need_update) {
    json_object_set (res, "is_blocked", json_boolean (U->full->is_blocked));
    if (U->full->about) {
      json_object_set (res, "about", json_string (U->full->about));
    }
    if (U->full->bot_info) {
      json_object_set (res, "bot_info", json_pack_bot_info (U->full->bot_info));
    }
  }

  return res;
}

json_t *json_pack_read (struct tdl_message *M) {
  return json_object ();
}

json_t *json_pack_group (struct tdl_group *G) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("group"));
  json_object_set (res, "id", json_integer (G->id));
  json_object_set (res, "members_count", json_integer (G->members_count));
  switch (G->role) {
  case tdl_chat_member_role_creator:
    json_object_set (res, "role", json_string ("creator"));
    break;
  case tdl_chat_member_role_editor:
    json_object_set (res, "role", json_string ("editor"));
    break;
  case tdl_chat_member_role_moderator:
    json_object_set (res, "role", json_string ("moderator"));
    break;
  case tdl_chat_member_role_general:
    json_object_set (res, "role", json_string ("general"));
    break;
  case tdl_chat_member_role_left:
    json_object_set (res, "role", json_string ("left"));
    break;
  case tdl_chat_member_role_kicked:
    json_object_set (res, "role", json_string ("kicked"));
    break;
  }
  json_object_set (res, "anyone_can_edit", json_boolean (G->anyone_can_edit));
  json_object_set (res, "is_active", json_boolean (G->is_active));
  json_object_set (res, "migrated_to_channel_id", json_integer (G->migrated_to_channel_id));

  if (G->full && !G->full->need_update) {
    if (G->full->invite_link) {
      json_object_set (res, "invite_link", json_string (G->full->invite_link));
    }
    json_object_set (res, "creator_user_id", json_integer (G->full->creator_user_id));

    json_t *arr = json_array ();
    int i;
    for (i = 0; i < G->full->members_cnt; i++) {
      json_array_append (arr, json_pack_chat_member (G->full->members[i]));
    }
    json_object_set (res, "members", arr);
  }
  return res;
}

json_t *json_pack_channel (struct tdl_channel *Ch) {
  json_t *res = json_object ();
  json_object_set (res, "type", json_string ("channel"));
  json_object_set (res, "id", json_integer (Ch->id));
  if (Ch->username) {
    json_object_set (res, "username", json_string (Ch->username));
  }
  if (Ch->date) {
    json_object_set (res, "date", json_integer (Ch->date));
  }
  switch (Ch->role) {
  case tdl_chat_member_role_creator:
    json_object_set (res, "role", json_string ("creator"));
    break;
  case tdl_chat_member_role_editor:
    json_object_set (res, "role", json_string ("editor"));
    break;
  case tdl_chat_member_role_moderator:
    json_object_set (res, "role", json_string ("moderator"));
    break;
  case tdl_chat_member_role_general:
    json_object_set (res, "role", json_string ("general"));
    break;
  case tdl_chat_member_role_left:
    json_object_set (res, "role", json_string ("left"));
    break;
  case tdl_chat_member_role_kicked:
    json_object_set (res, "role", json_string ("kicked"));
    break;
  }

  json_object_set (res, "anyone_can_invite", json_boolean (Ch->anyone_can_invite));
  json_object_set (res, "sign_messages", json_boolean (Ch->sign_messages));
  json_object_set (res, "is_broadcast", json_boolean (Ch->is_broadcast));
  json_object_set (res, "is_supergroup", json_boolean (Ch->is_supergroup));
  json_object_set (res, "is_verified", json_boolean (Ch->is_verified));

  if (Ch->restriction_reason) {
    json_object_set (res, "restriction_reason", json_string (Ch->restriction_reason));
  }
  
  if (Ch->full && !Ch->full->need_update ) {
    if (Ch->full->about) {
      json_object_set (res, "about", json_string (Ch->full->about));
    }
    json_object_set (res, "members_cnt", json_integer (Ch->full->members_cnt));
    json_object_set (res, "admins_cnt", json_integer (Ch->full->admins_cnt));
    json_object_set (res, "kicked_cnt", json_integer (Ch->full->kicked_cnt));
    json_object_set (res, "can_get_members", json_boolean (Ch->full->can_get_members));
    json_object_set (res, "can_set_username", json_boolean (Ch->full->can_set_username));
    if (Ch->full->invite_link) {
      json_object_set (res, "invite_link", json_string (Ch->full->invite_link));
    }
    if (Ch->full->migrated_from_group_id) {
      json_object_set (res, "migrated_from_group_id", json_integer (Ch->full->migrated_from_group_id));
      json_object_set (res, "migrated_from_max_message_id", json_integer (Ch->full->migrated_from_max_message_id));
    }
  }
  return res;
}

json_t *json_pack_secret_chat (struct tdl_secret_chat *SC) {
  return json_object ();
}


#endif