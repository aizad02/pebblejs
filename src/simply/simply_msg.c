#include "simply_msg.h"

#include "simply_accel.h"
#include "simply_res.h"
#include "simply_stage.h"
#include "simply_menu.h"
#include "simply_ui.h"
#include "simply_window_stack.h"

#include "simply.h"

#include "util/dict.h"
#include "util/list1.h"
#include "util/math.h"
#include "util/memory.h"
#include "util/string.h"
#include "util/window.h"

#include <pebble.h>

#define SEND_DELAY_MS 10

typedef enum Command Command;

enum Command {
  CommandWindowShow = 1,
  CommandWindowHide,
  CommandWindowProps,
  CommandWindowActionBar,
  CommandCardClear,
  CommandCardText,
  CommandCardImage,
  CommandCardStyle,
  CommandVibe,
  CommandAccelPeek,
  CommandAccelConfig,
};

typedef enum WindowType WindowType;

enum WindowType {
  WindowTypeWindow = 0,
  WindowTypeMenu,
  WindowTypeCard,
  WindowTypeLast,
};

typedef enum VibeType VibeType;

enum VibeType {
  VibeShort = 0,
  VibeLong = 1,
  VibeDouble = 2,
};

typedef struct Packet Packet;

struct __attribute__((__packed__)) Packet {
  Command type:16;
  uint16_t length;
};

typedef struct WindowShowPacket WindowShowPacket;

struct __attribute__((__packed__)) WindowShowPacket {
  Packet packet;
  WindowType type:8;
  bool pushing;
};

typedef struct WindowHidePacket WindowHidePacket;

struct __attribute__((__packed__)) WindowHidePacket {
  Packet packet;
  uint32_t id;
};

typedef struct WindowPropsPacket WindowPropsPacket;

struct __attribute__((__packed__)) WindowPropsPacket {
  Packet packet;
  uint32_t id;
  GColor background_color:8;
  bool fullscreen;
  bool scrollable;
};

typedef struct WindowActionBarPacket WindowActionBarPacket;

struct __attribute__((__packed__)) WindowActionBarPacket {
  Packet packet;
  uint32_t image[3];
  GColor background_color:8;
  bool action;
};

typedef struct CardClearPacket CardClearPacket;

struct __attribute__((__packed__)) CardClearPacket {
  Packet packet;
  uint8_t flags;
};

typedef struct CardTextPacket CardTextPacket;

struct __attribute__((__packed__)) CardTextPacket {
  Packet packet;
  uint8_t index;
  char text[];
};

typedef struct CardImagePacket CardImagePacket;

struct __attribute__((__packed__)) CardImagePacket {
  Packet packet;
  uint32_t image;
  uint8_t index;
};

typedef struct CardStylePacket CardStylePacket;

struct __attribute__((__packed__)) CardStylePacket {
  Packet packet;
  uint8_t style;
};

typedef struct VibePacket VibePacket;

struct __attribute__((__packed__)) VibePacket {
  Packet packet;
  VibeType type:8;
};

typedef Packet AccelPeekPacket;

typedef struct AccelConfigPacket AccelConfigPacket;

struct __attribute__((__packed__)) AccelConfigPacket {
  Packet packet;
  uint16_t num_samples;
  AccelSamplingRate rate:8;
  bool data_subscribed;
};

typedef enum SimplyACmd SimplyACmd;

enum SimplyACmd {
  SimplyACmd_setWindow = 0,
  SimplyACmd_windowShow,
  SimplyACmd_windowHide,
  SimplyACmd_setUi,
  SimplyACmd_click,
  SimplyACmd_longClick,
  SimplyACmd_accelTap,
  SimplyACmd_vibe,
  SimplyACmd_accelData,
  SimplyACmd_getAccelData,
  SimplyACmd_configAccelData,
  SimplyACmd_configButtons,
  SimplyACmd_setMenu,
  SimplyACmd_setMenuSection,
  SimplyACmd_getMenuSection,
  SimplyACmd_setMenuItem,
  SimplyACmd_getMenuItem,
  SimplyACmd_menuSelect,
  SimplyACmd_menuLongSelect,
  SimplyACmd_image,
  SimplyACmd_setStage,
  SimplyACmd_stageElement,
  SimplyACmd_stageRemove,
  SimplyACmd_stageAnimate,
  SimplyACmd_stageAnimateDone,
};

typedef enum SimplySetUiParam SimplySetUiParam;

enum SimplySetUiParam {
  SetUi_clear = 1,
  SetUi_title,
  SetUi_subtitle,
  SetUi_body,
  SetUi_icon,
  SetUi_subicon,
  SetUi_image,
  SetUi_style,
};

typedef enum SimplySetMenuParam SimplySetMenuParam;

enum SimplySetMenuParam {
  SetMenu_clear = 1,
  SetMenu_sections,
};

typedef enum ElementParam ElementParam;

enum ElementParam {
  ElementId = 1,
  ElementType,
  ElementIndex,
  ElementX,
  ElementY,
  ElementWidth,
  ElementHeight,
  ElementBackgroundColor,
  ElementBorderColor,
  ElementRadius,
  ElementText,
  ElementTextFont,
  ElementTextColor,
  ElementTextOverflow,
  ElementTextAlignment,
  ElementTextUpdateTimeUnit,
  ElementImage,
  ElementCompositing,
};

static bool s_has_communicated = false;

bool simply_msg_has_communicated() {
  return s_has_communicated;
}

static SimplyWindow *get_top_simply_window(Simply *simply) {
  Window *base_window = window_stack_get_top_window();
  SimplyWindow *window = window_get_user_data(base_window);
  if (!window || (void*) window == simply->splash) {
    return NULL;
  }
  return window;
}

static void handle_config_buttons(DictionaryIterator *iter, Simply *simply) {
  SimplyWindow *window = get_top_simply_window(simply);
  if (!window) {
    return;
  }
  Tuple *tuple;
  for (int i = 0; i < NUM_BUTTONS; ++i) {
    if ((tuple = dict_find(iter, i + 1))) {
      simply_window_set_button(window, i, tuple->value->int32);
    }
  }
}

static void handle_set_menu(DictionaryIterator *iter, Simply *simply) {
  SimplyMenu *menu = simply->menu;
  Tuple *tuple;
  for (tuple = dict_read_first(iter); tuple; tuple = dict_read_next(iter)) {
    switch (tuple->key) {
      case SetMenu_clear:
        simply_menu_clear(menu);
        break;
      case SetMenu_sections:
        simply_menu_set_num_sections(menu, tuple->value->int32);
        break;
    }
  }
}

static void handle_set_menu_section(DictionaryIterator *iter, Simply *simply) {
  Tuple *tuple;
  uint16_t section_index = 0;
  uint16_t num_items = 1;
  char *title = NULL;
  if ((tuple = dict_find(iter, 2))) {
    section_index = tuple->value->uint16;
  }
  if ((tuple = dict_find(iter, 3))) {
    num_items = tuple->value->uint16;
  }
  if ((tuple = dict_find(iter, 4))) {
    title = tuple->value->cstring;
  }
  if ((tuple = dict_find(iter, 1))) {
    simply_menu_clear_section_items(simply->menu, section_index);
  }
  SimplyMenuSection *section = malloc(sizeof(*section));
  *section = (SimplyMenuSection) {
    .section = section_index,
    .num_items = num_items,
    .title = strdup2(title),
  };
  simply_menu_add_section(simply->menu, section);
}

static void handle_set_menu_item(DictionaryIterator *iter, Simply *simply) {
  Tuple *tuple;
  uint16_t section_index = 0;
  uint16_t row = 0;
  uint32_t icon = 0;
  char *title = NULL;
  char *subtitle = NULL;
  if ((tuple = dict_find(iter, 1))) {
    section_index = tuple->value->uint16;
  }
  if ((tuple = dict_find(iter, 2))) {
    row = tuple->value->uint16;
  }
  if ((tuple = dict_find(iter, 3))) {
    title = tuple->value->cstring;
  }
  if ((tuple = dict_find(iter, 4))) {
    subtitle = tuple->value->cstring;
  }
  if ((tuple = dict_find(iter, 5))) {
    icon = tuple->value->uint32;
  }
  SimplyMenuItem *item = malloc(sizeof(*item));
  *item = (SimplyMenuItem) {
    .section = section_index,
    .item = row,
    .title = strdup2(title),
    .subtitle = strdup2(subtitle),
    .icon = icon,
  };
  simply_menu_add_item(simply->menu, item);
}

static void handle_set_image(DictionaryIterator *iter, Simply *simply) {
  Tuple *tuple;
  uint32_t id = 0;
  int16_t width = 0;
  int16_t height = 0;
  uint32_t *pixels = NULL;
  if ((tuple = dict_find(iter, 1))) {
    id = tuple->value->uint32;
  }
  if ((tuple = dict_find(iter, 2))) {
    width = tuple->value->int16;
  }
  if ((tuple = dict_find(iter, 3))) {
    height = tuple->value->int16;
  }
  if ((tuple = dict_find(iter, 4))) {
    pixels = (uint32_t*) tuple->value->data;
  }
  simply_res_add_image(simply->res, id, width, height, pixels);
}

static void handle_set_stage(DictionaryIterator *iter, Simply *simply) {
  SimplyStage *stage = simply->stage;
  Tuple *tuple;
  for (tuple = dict_read_first(iter); tuple; tuple = dict_read_next(iter)) {
    switch (tuple->key) {
      case 0:
        simply_stage_clear(stage);
        break;
    }
  }
}

static void handle_set_stage_element(DictionaryIterator *iter, Simply *simply) {
  SimplyStage *stage = simply->stage;
  Tuple *tuple;
  uint32_t id = 0;
  SimplyElementType type = SimplyElementTypeNone;
  if ((tuple = dict_find(iter, ElementId))) {
    id = tuple->value->uint32;
  }
  if ((tuple = dict_find(iter, ElementType))) {
    type = tuple->value->int32;
  }
  SimplyElementCommon *element = simply_stage_auto_element(stage, id, type);
  if (!element || element->type != type) {
    return;
  }
  GRect frame = element->frame;
  bool update_frame = false;
  bool update_ticker = false;
  for (tuple = dict_read_first(iter); tuple; tuple = dict_read_next(iter)) {
    switch (tuple->key) {
      case ElementIndex:
        simply_stage_insert_element(stage, tuple->value->uint16, element);
        break;
      case ElementX:
        frame.origin.x = tuple->value->int16;
        update_frame = true;
        break;
      case ElementY:
        frame.origin.y = tuple->value->int16;
        update_frame = true;
        break;
      case ElementWidth:
        frame.size.w = tuple->value->uint16;
        update_frame = true;
        break;
      case ElementHeight:
        frame.size.h = tuple->value->uint16;
        update_frame = true;
        break;
      case ElementBackgroundColor:
        element->background_color = tuple->value->uint8;
        break;
      case ElementBorderColor:
        element->border_color = tuple->value->uint8;
        break;
      case ElementRadius:
        ((SimplyElementRect*) element)->radius = tuple->value->uint16;
        break;
      case ElementText:
        strset(&((SimplyElementText*) element)->text, tuple->value->cstring);
        break;
      case ElementTextFont:
        if (tuple->type == TUPLE_CSTRING) {
          ((SimplyElementText*) element)->font = fonts_get_system_font(tuple->value->cstring);
        } else {
          ((SimplyElementText*) element)->font = simply_res_get_font(simply->res, tuple->value->uint32);
        }
        break;
      case ElementTextColor:
        ((SimplyElementText*) element)->text_color = tuple->value->uint8;
        break;
      case ElementTextOverflow:
        ((SimplyElementText*) element)->overflow_mode = tuple->value->uint8;
        break;
      case ElementTextAlignment:
        ((SimplyElementText*) element)->alignment = tuple->value->uint8;
        break;
      case ElementTextUpdateTimeUnit:
        ((SimplyElementText*) element)->time_units = tuple->value->uint8;
        update_ticker = true;
        break;
      case ElementImage:
        ((SimplyElementImage*) element)->image = tuple->value->uint32;
        break;
      case ElementCompositing:
        ((SimplyElementImage*) element)->compositing = tuple->value->uint8;
        break;
    }
  }
  if (update_frame) {
    simply_stage_set_element_frame(stage, element, frame);
  }
  if (update_ticker) {
    simply_stage_update_ticker(stage);
  }
  simply_stage_update(stage);
}

static void handle_remove_stage_element(DictionaryIterator *iter, Simply *simply) {
  SimplyStage *stage = simply->stage;
  Tuple *tuple;
  uint32_t id = 0;
  if ((tuple = dict_find(iter, 1))) {
    id = tuple->value->uint32;
  }
  SimplyElementCommon *element = simply_stage_get_element(stage, id);
  if (!element) {
    return;
  }
  simply_stage_remove_element(stage, element);
  simply_stage_update(stage);
}

static void handle_animate_stage_element(DictionaryIterator *iter, Simply *simply) {
  SimplyStage *stage = simply->stage;
  Tuple *tuple;
  uint32_t id = 0;
  if ((tuple = dict_find(iter, 1))) {
    id = tuple->value->uint32;
  }
  SimplyElementCommon *element = simply_stage_get_element(stage, id);
  if (!element) {
    return;
  }
  GRect to_frame = element->frame;
  SimplyAnimation *animation = malloc0(sizeof(*animation));
  if (!animation) {
    return;
  }
  if ((tuple = dict_find(iter, 2))) {
    to_frame.origin.x = tuple->value->int16;
  }
  if ((tuple = dict_find(iter, 3))) {
    to_frame.origin.y = tuple->value->int16;
  }
  if ((tuple = dict_find(iter, 4))) {
    to_frame.size.w = tuple->value->int16;
  }
  if ((tuple = dict_find(iter, 5))) {
    to_frame.size.h = tuple->value->int16;
  }
  if ((tuple = dict_find(iter, 6))) {
    animation->duration = tuple->value->uint32;
  }
  if ((tuple = dict_find(iter, 7))) {
    animation->curve = tuple->value->uint8;
  }
  simply_stage_animate_element(stage, element, animation, to_frame);
}

static void handle_window_show_packet(Simply *simply, Packet *data) {
  WindowShowPacket *packet = (WindowShowPacket*) data;
  SimplyWindow *window = simply->windows[MIN(WindowTypeLast - 1, packet->type)];
  simply_window_stack_show(simply->window_stack, window, packet->pushing);
}

static void handle_window_hide_packet(Simply *simply, Packet *data) {
  WindowHidePacket *packet = (WindowHidePacket*) data;
  SimplyWindow *window = get_top_simply_window(simply);
  if (!window) {
    return;
  }
  if (window->id == packet->id) {
    simply_window_stack_pop(simply->window_stack, window);
  }
}

static void handle_window_props_packet(Simply *simply, Packet *data) {
  WindowPropsPacket *packet = (WindowPropsPacket*) data;
  SimplyWindow *window = get_top_simply_window(simply);
  if (!window) {
    return;
  }
  window->id = packet->id;
  simply_window_set_background_color(window, packet->background_color);
  simply_window_set_fullscreen(window, packet->fullscreen);
  simply_window_set_scrollable(window, packet->scrollable);
}

static void handle_window_action_bar_packet(Simply *simply, Packet *data) {
  WindowActionBarPacket *packet = (WindowActionBarPacket*) data;
  SimplyWindow *window = get_top_simply_window(simply);
  if (!window) {
    return;
  }
  for (unsigned int i = 0; i < ARRAY_LENGTH(packet->image); ++i) {
    simply_window_set_action_bar_icon(window, i, packet->image[i]);
  }
  simply_window_set_action_bar_background_color(window, packet->background_color);
  simply_window_set_action_bar(window, packet->action);
}

static void handle_card_clear_packet(Simply *simply, Packet *data) {
  CardClearPacket *packet = (CardClearPacket*) data;
  simply_ui_clear(simply->ui, packet->flags);
}

static void handle_card_text_packet(Simply *simply, Packet *data) {
  CardTextPacket *packet = (CardTextPacket*) data;
  simply_ui_set_text(simply->ui, MIN(NumUiTextfields - 1, packet->index), packet->text);
}

static void handle_card_image_packet(Simply *simply, Packet *data) {
  CardImagePacket *packet = (CardImagePacket*) data;
  simply->ui->ui_layer.imagefields[MIN(NumUiImagefields - 1, packet->index)] = packet->image;
  window_stack_schedule_top_window_render();
}

static void handle_card_style_packet(Simply *simply, Packet *data) {
  CardStylePacket *packet = (CardStylePacket*) data;
  simply_ui_set_style(simply->ui, packet->style);
}

static void handle_vibe_packet(Simply *simply, Packet *data) {
  VibePacket *packet = (VibePacket*) data;
  switch (packet->type) {
    case VibeShort: vibes_short_pulse(); break;
    case VibeLong: vibes_short_pulse(); break;
    case VibeDouble: vibes_double_pulse(); break;
  }
}

static void accel_peek_timer_callback(void *context) {
  Simply *simply = context;
  AccelData data = { .x = 0 };
  simply_accel_peek(simply->accel, &data);
  if (!simply_msg_accel_data(simply->msg, &data, 1, 0)) {
    app_timer_register(10, accel_peek_timer_callback, simply);
  }
}

static void handle_accel_peek_packet(Simply *simply, Packet *data) {
  app_timer_register(10, accel_peek_timer_callback, simply);
}

static void handle_accel_config_packet(Simply *simply, Packet *data) {
  AccelConfigPacket *packet = (AccelConfigPacket*) data;
  simply->accel->num_samples = packet->num_samples;
  simply->accel->rate = packet->rate;
  simply_accel_set_data_subscribe(simply->accel, packet->data_subscribed);
}

static void handle_packet(Simply *simply, uint8_t *buffer, uint16_t length) {
  Packet *packet = (Packet*) buffer;
  switch (packet->type) {
    case CommandWindowShow:
      handle_window_show_packet(simply, packet);
      break;
    case CommandWindowHide:
      handle_window_hide_packet(simply, packet);
      break;
    case CommandWindowProps:
      handle_window_props_packet(simply, packet);
      break;
    case CommandWindowActionBar:
      handle_window_action_bar_packet(simply, packet);
      break;
    case CommandCardClear:
      handle_card_clear_packet(simply, packet);
      break;
    case CommandCardText:
      handle_card_text_packet(simply, packet);
      break;
    case CommandCardImage:
      handle_card_image_packet(simply, packet);
      break;
    case CommandCardStyle:
      handle_card_style_packet(simply, packet);
      break;
    case CommandVibe:
      handle_vibe_packet(simply, packet);
      break;
    case CommandAccelPeek:
      handle_accel_peek_packet(simply, packet);
      break;
    case CommandAccelConfig:
      handle_accel_config_packet(simply, packet);
      break;
  }
}

static void received_callback(DictionaryIterator *iter, void *context) {
  Tuple *tuple = dict_find(iter, 0);
  if (!tuple) {
    return;
  }

  s_has_communicated = true;

  if (tuple->value->uint32 > 0xFFFF) {
    handle_packet(context, tuple->value->data, tuple->length);
  }

  switch (tuple->value->uint8) {
    case SimplyACmd_configButtons:
      handle_config_buttons(iter, context);
      break;
    case SimplyACmd_setMenu:
      handle_set_menu(iter, context);
      break;
    case SimplyACmd_setMenuSection:
      handle_set_menu_section(iter, context);
      break;
    case SimplyACmd_setMenuItem:
      handle_set_menu_item(iter, context);
      break;
    case SimplyACmd_image:
      handle_set_image(iter, context);
      break;
    case SimplyACmd_setStage:
      handle_set_stage(iter, context);
      break;
    case SimplyACmd_stageElement:
      handle_set_stage_element(iter, context);
      break;
    case SimplyACmd_stageRemove:
      handle_remove_stage_element(iter, context);
      break;
    case SimplyACmd_stageAnimate:
      handle_animate_stage_element(iter, context);
      break;
  }
}

static void dropped_callback(AppMessageResult reason, void *context) {
}

static void sent_callback(DictionaryIterator *iter, void *context) {
}

static void failed_callback(DictionaryIterator *iter, AppMessageResult reason, Simply *simply) {
  SimplyUi *ui = simply->ui;
  if (reason == APP_MSG_NOT_CONNECTED) {
    simply_ui_clear(ui, ~0);
    simply_ui_set_text(ui, UiSubtitle, "Disconnected");
    simply_ui_set_text(ui, UiBody, "Run the Pebble Phone App");
    simply_window_stack_show(simply->window_stack, &ui->window, true);
  }
}

SimplyMsg *simply_msg_create(Simply *simply) {
  SimplyMsg *self = malloc(sizeof(*self));
  *self = (SimplyMsg) { .simply = simply };

  simply->msg = self;

  const uint32_t size_inbound = 2048;
  const uint32_t size_outbound = 512;
  app_message_open(size_inbound, size_outbound);

  app_message_set_context(simply);

  app_message_register_inbox_received(received_callback);
  app_message_register_inbox_dropped(dropped_callback);
  app_message_register_outbox_sent(sent_callback);
  app_message_register_outbox_failed((AppMessageOutboxFailed) failed_callback);

  return self;
}

void simply_msg_destroy(SimplyMsg *self) {
  if (!self) {
    return;
  }

  app_message_deregister_callbacks();

  self->simply->msg = NULL;

  free(self);
}

static void destroy_packet(SimplyPacket *packet) {
  if (!packet) {
    return;
  }
  free(packet->buffer);
  packet->buffer = NULL;
  free(packet);
}

static bool send_msg(SimplyPacket *packet) {
  DictionaryIterator *iter = NULL;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    return false;
  }
  dict_copy_from_buffer(iter, packet->buffer, packet->length);
  return (app_message_outbox_send() == APP_MSG_OK);
}

static void send_msg_retry(void *data) {
  SimplyMsg *self = data;
  SimplyPacket *packet = (SimplyPacket*) self->queue;
  if (!packet) {
    return;
  }
  if (!send_msg(packet)){
    self->send_delay_ms *= 2;
    app_timer_register(self->send_delay_ms, send_msg_retry, self);
    return;
  }
  list1_remove(&self->queue, &packet->node);
  destroy_packet(packet);
  self->send_delay_ms = SEND_DELAY_MS;
}

static SimplyPacket *add_packet(SimplyMsg *self, void *buffer, size_t length) {
  SimplyPacket *packet = malloc0(sizeof(*packet));
  if (!packet) {
    free(buffer);
    return NULL;
  }
  *packet = (SimplyPacket) {
    .length = length,
    .buffer = buffer,
  };
  list1_append(&self->queue, &packet->node);
  send_msg_retry(self);
  return packet;
}

static bool send_click(SimplyMsg *self, SimplyACmd type, ButtonId button) {
  size_t length = dict_calc_buffer_size(2, 1, 1);
  void *buffer = malloc0(length);
  if (!buffer) {
    return false;
  }
  DictionaryIterator iter;
  dict_write_begin(&iter, buffer, length);
  dict_write_uint8(&iter, 0, type);
  dict_write_uint8(&iter, 1, button);
  return add_packet(self, buffer, length);
}

bool simply_msg_single_click(SimplyMsg *self, ButtonId button) {
  return send_click(self, SimplyACmd_click, button);
}

bool simply_msg_long_click(SimplyMsg *self, ButtonId button) {
  return send_click(self, SimplyACmd_longClick, button);
}

bool send_window(SimplyMsg *self, SimplyACmd type, uint32_t id) {
  size_t length = dict_calc_buffer_size(2, 1, 4);
  void *buffer = malloc0(length);
  if (!buffer) {
    return false;
  }
  DictionaryIterator iter;
  dict_write_begin(&iter, buffer, length);
  dict_write_uint8(&iter, 0, type);
  dict_write_uint32(&iter, 1, id);
  return add_packet(self, buffer, length);
}

bool simply_msg_window_show(SimplyMsg *self, uint32_t id) {
  return send_window(self, SimplyACmd_windowShow, id);
}

bool simply_msg_window_hide(SimplyMsg *self, uint32_t id) {
  return send_window(self, SimplyACmd_windowHide, id);
}

bool simply_msg_accel_tap(SimplyMsg *self, AccelAxisType axis, int32_t direction) {
  size_t length = dict_calc_buffer_size(3, 1, 1, 1);
  void *buffer = malloc0(length);
  if (!buffer) {
    return false;
  }
  DictionaryIterator iter;
  dict_write_begin(&iter, buffer, length);
  dict_write_uint8(&iter, 0, SimplyACmd_accelTap);
  dict_write_uint8(&iter, 1, axis);
  dict_write_int8(&iter, 2, direction);
  return add_packet(self, buffer, length);
}

bool simply_msg_accel_data(SimplyMsg *self, AccelData *data, uint32_t num_samples, int32_t transaction_id) {
  DictionaryIterator *iter = NULL;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    return false;
  }
  dict_write_uint8(iter, 0, SimplyACmd_accelData);
  if (transaction_id >= 0) {
    dict_write_int32(iter, 1, transaction_id);
  }
  dict_write_uint8(iter, 2, num_samples);
  dict_write_data(iter, 3, (uint8_t*) data, sizeof(*data) * num_samples);
  return (app_message_outbox_send() == APP_MSG_OK);
}

static void write_menu_item(DictionaryIterator *iter, SimplyACmd type, uint16_t section, uint16_t index) {
  dict_write_uint8(iter, 0, type);
  dict_write_uint16(iter, 1, section);
  dict_write_uint16(iter, 2, index);
}

static bool send_menu_item(SimplyMsg *self, SimplyACmd type, uint16_t section, uint16_t index) {
  DictionaryIterator *iter = NULL;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    return false;
  }
  write_menu_item(iter, type, section, index);
  return (app_message_outbox_send() == APP_MSG_OK);
}

static bool send_menu_item_retry(SimplyMsg *self, SimplyACmd type, uint16_t section, uint16_t index) {
  size_t length = dict_calc_buffer_size(3, 1, 2, 2);
  void *buffer = malloc0(length);
  if (!buffer) {
    return false;
  }
  DictionaryIterator iter;
  dict_write_begin(&iter, buffer, length);
  write_menu_item(&iter, type, section, index);
  return add_packet(self, buffer, length);
}

bool simply_msg_menu_get_section(SimplyMsg *self, uint16_t index) {
  return send_menu_item(self, SimplyACmd_getMenuSection, index, 0);
}

bool simply_msg_menu_get_item(SimplyMsg *self, uint16_t section, uint16_t index) {
  return send_menu_item(self, SimplyACmd_getMenuItem, section, index);
}

bool simply_msg_menu_select_click(SimplyMsg *self, uint16_t section, uint16_t index) {
  return send_menu_item_retry(self, SimplyACmd_menuSelect, section, index);
}

bool simply_msg_menu_select_long_click(SimplyMsg *self, uint16_t section, uint16_t index) {
  return send_menu_item_retry(self, SimplyACmd_menuLongSelect, section, index);
}

bool simply_msg_animate_element_done(SimplyMsg *self, uint16_t index) {
  size_t length = dict_calc_buffer_size(2, 1, 2);
  void *buffer = malloc0(length);
  if (!buffer) {
    return false;
  }
  DictionaryIterator iter;
  dict_write_begin(&iter, buffer, length);
  dict_write_uint8(&iter, 0, SimplyACmd_stageAnimateDone);
  dict_write_uint16(&iter, 1, index);
  return add_packet(self, buffer, length);
}
