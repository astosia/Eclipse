#include <pebble.h>
#include <pebble-fctx/fctx.h>
#include <pebble-fctx/fpath.h>
#include <pebble-fctx/ffont.h>

// ── Persistent settings ───────────────────────────────────────────────────────

#define SETTINGS_KEY 1

// Forward declarations for variables and functions used before their definition
static int s_hours, s_minutes, s_day, s_month;
static int s_current_frame_index = -1;
static void prv_load_frame(int frame_index);

typedef struct EclipseSettings {
    int theme;
    GColor custom_bg;
    GColor custom_lg;
    GColor custom_dg;
    GColor custom_fg;
    bool RemoveZero24h;
    bool AddZero12h;
    bool BTVibeOn;
    bool showlocalAMPM;
    GColor time_text_color;
    GColor other_text_color;
    GColor line_color;
    char LogoText[15]; 
    bool ShowLine;
    bool ShowText;  
    bool ShowTime;
    bool ShowInfo;
} __attribute__((__packed__)) EclipseSettings;

static EclipseSettings s_settings;

static void prv_default_settings(void) {
    s_settings.theme     = 0;
    s_settings.custom_bg = GColorWhite;
    s_settings.custom_lg = GColorLightGray;
    s_settings.custom_dg = GColorDarkGray;
    s_settings.custom_fg = GColorBlack;
    s_settings.BTVibeOn = true;
    s_settings.RemoveZero24h = false;
    s_settings.AddZero12h = false;
    s_settings.showlocalAMPM = true;
    s_settings.time_text_color = GColorBlack;
    s_settings.other_text_color = GColorBlack;
    s_settings.line_color = GColorBlack;
    snprintf(s_settings.LogoText, sizeof(s_settings.LogoText), "%s", "DIVERGENCE");
    s_settings.ShowLine = true;
    s_settings.ShowText = true;
    s_settings.ShowTime = true;
    s_settings.ShowInfo = true;
}

static void prv_load_settings(void) {
    prv_default_settings();
    persist_read_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
    // Initialise time from clock so first draw is never garbage
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    s_hours = t->tm_hour;
    s_minutes = t->tm_min;
    s_day = t->tm_mday;
    s_month = t->tm_mon;
}

static void prv_save_settings(void) {
    persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
}

// ── Display / frame ───────────────────────────────────────────────────────────

static Window  *s_window;
static Layer   *s_canvas_layer;
static GBitmap *s_frame_bitmap;
static Layer   *s_time_layer;
static Layer   *s_canvas_qt_icon;
static Layer   *s_canvas_bt_icon;
FFont          *time_ffont;
static GFont    FontBTQTIcons;
bool connected = true;
#define FRAME_WIDTH  260
#define FRAME_HEIGHT 260
#define TOTAL_FRAMES 30

typedef struct { int x, y; } UIOffset;
typedef struct {
    int time_font_size;
    int info_font_size;
    int other_text_font_size;
} UIConfig;

#ifdef PBL_PLATFORM_EMERY
static const UIOffset s_offset = { -30, -16 };
static const UIConfig s_config = { .time_font_size = 70, .info_font_size = 24, .other_text_font_size = 16 };
#else
static const UIOffset s_offset = {   0,   0 };
static const UIConfig s_config = { .time_font_size = 70, .info_font_size = 24, .other_text_font_size = 16 };
#endif

static const uint32_t s_frame_resources[TOTAL_FRAMES] = {
    RESOURCE_ID_FRAME_00, RESOURCE_ID_FRAME_01, RESOURCE_ID_FRAME_02,
    RESOURCE_ID_FRAME_03, RESOURCE_ID_FRAME_04, RESOURCE_ID_FRAME_05,
    RESOURCE_ID_FRAME_06, RESOURCE_ID_FRAME_07, RESOURCE_ID_FRAME_08,
    RESOURCE_ID_FRAME_09, RESOURCE_ID_FRAME_10, RESOURCE_ID_FRAME_11,
    RESOURCE_ID_FRAME_12, RESOURCE_ID_FRAME_13, RESOURCE_ID_FRAME_14,
    RESOURCE_ID_FRAME_15, RESOURCE_ID_FRAME_16, RESOURCE_ID_FRAME_17,
    RESOURCE_ID_FRAME_18, RESOURCE_ID_FRAME_19, RESOURCE_ID_FRAME_20,
    RESOURCE_ID_FRAME_21, RESOURCE_ID_FRAME_22, RESOURCE_ID_FRAME_23,
    RESOURCE_ID_FRAME_24, RESOURCE_ID_FRAME_25, RESOURCE_ID_FRAME_26,
    RESOURCE_ID_FRAME_27, RESOURCE_ID_FRAME_28, RESOURCE_ID_FRAME_29
};

// ── Colour Lookup Table (LUT) ────────────────────────────────────────────────────────────────

typedef struct { uint8_t bg, lg, dg, fg; } ColourTheme;

static const ColourTheme s_presets[] = {
    { 0xFF, 0xEA, 0xD5, 0xC0 },  // 0 Original
    { 0xC0, 0xD5, 0xEA, 0xFF },  // 1 Inverted
    { 0xFF, 0xD7, 0xC3, 0xC1 },  // 2 Cobalt
    { 0xC0, 0xF4, 0xF8, 0xFC },  // 3 Ember
    { 0xC4, 0xD9, 0xDC, 0xDF },  // 4 Forest
};

static uint8_t s_lut[256];

static void prv_rebuild_lut(void) {
    int idx = s_settings.theme;
    if (idx < 0 || idx > 5) idx = 0;

    ColourTheme t;
    if (idx == 5) {
        t.bg = s_settings.custom_bg.argb;
        t.lg = s_settings.custom_lg.argb;
        t.dg = s_settings.custom_dg.argb;
        t.fg = s_settings.custom_fg.argb;
    } else {
        t = s_presets[idx];
    }

    // Build a Luminance-based LUT: map all 256 possible colors to the 4 slots
    for (int i = 0; i < 256; i++) {
        // Extract 2-bit R,G,B (0-3 range each) from the GColor8 byte
        uint8_t r = (i >> 4) & 0x03;
        uint8_t g = (i >> 2) & 0x03;
        uint8_t b = i & 0x03;
        
        // Intensity score from 0 (black) to 9 (white)
        uint16_t intensity = r + g + b;

        if (intensity >= 8)      s_lut[i] = t.bg; // Brighter colors -> Background
        else if (intensity >= 5) s_lut[i] = t.lg; // Mid-bright -> Light Grey
        else if (intensity >= 2) s_lut[i] = t.dg; // Mid-dark -> Dark Grey
        else                     s_lut[i] = t.fg; // Darkest -> Foreground
    }
}

static void prv_apply_lut(GBitmap *bmp) {
    if (!bmp) return;
    uint8_t *data = gbitmap_get_data(bmp);
    GRect bounds = gbitmap_get_bounds(bmp);
    int bpr = gbitmap_get_bytes_per_row(bmp);
    for (int y = 0; y < bounds.size.h; y++) {
        uint8_t *row = data + (y * bpr);
        for (int x = 0; x < bounds.size.w; x++) {
            row[x] = s_lut[row[x]];
        }
    }
}

// ── Frame loading ─────────────────────────────────────────────────────────────

static void prv_load_frame(int frame_index) {
    // Only reload if the frame has actually changed
    if (s_frame_bitmap && s_current_frame_index == frame_index) return;

    if (s_frame_bitmap) {
        gbitmap_destroy(s_frame_bitmap);
        s_frame_bitmap = NULL;
    }
    s_frame_bitmap = gbitmap_create_with_resource(s_frame_resources[frame_index]);
    if (!s_frame_bitmap) {
        APP_LOG(APP_LOG_LEVEL_ERROR, "OOM: failed to load frame %d", frame_index);
        s_current_frame_index = -1;
        return;
    }
    s_current_frame_index = frame_index;
    if (s_settings.theme != 0) {
        prv_apply_lut(s_frame_bitmap);
    }
}



// ── Eclipse Drawing ───────────────────────────────────────────────────────────────────

static void prv_canvas_update(Layer *layer, GContext *ctx) {
    if (s_frame_bitmap) {
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, s_frame_bitmap, GRect(s_offset.x, s_offset.y, FRAME_WIDTH, FRAME_HEIGHT));
    }
}

// ── Time layer draw (fctx) ────────────────────────────────────────────────────

static void layer_update_proc_time(Layer *layer, GContext *ctx) {
    if (!time_ffont) return;

    GRect bounds = layer_get_bounds(layer);

    // Free bitmap immediately before fctx init to make room for its framebuffer.
    // Reload it immediately after fctx deinit so the ring is never missing.
    if (s_frame_bitmap) {
        gbitmap_destroy(s_frame_bitmap);
        s_frame_bitmap = NULL;
    }

    FContext fctx;
    fctx_init_context(&fctx, ctx);
    fctx_set_color_bias(&fctx, 0);
    #ifdef PBL_COLOR
    fctx_enable_aa(true);
    #endif

    if(s_settings.ShowTime){
        fctx_set_fill_color(&fctx, s_settings.time_text_color);

        int hourdraw;
        char hournow[4];
        if (clock_is_24h_style()) {
            hourdraw = s_hours;
            if (s_settings.RemoveZero24h) {
                snprintf(hournow, sizeof(hournow), "%d", hourdraw);
            } else {
                snprintf(hournow, sizeof(hournow), "%02d", hourdraw);
            }
        } else {
            if (s_hours == 0 || s_hours == 12) hourdraw = 12;
            else hourdraw = s_hours % 12;
            if (s_settings.AddZero12h) {
                snprintf(hournow, sizeof(hournow), "%02d", hourdraw);
            } else {
                snprintf(hournow, sizeof(hournow), "%d", hourdraw);
            }
        }

        char minnow[3];
        snprintf(minnow, sizeof(minnow), "%02d", s_minutes);

        char timedraw[8];
        snprintf(timedraw, sizeof(timedraw), "%s:%s", hournow, minnow);

        FPoint time_pos;
        time_pos.x = INT_TO_FIXED(bounds.size.w / 2);
        time_pos.y = INT_TO_FIXED(bounds.size.h / 2);

        fctx_begin_fill(&fctx);
        fctx_set_text_em_height(&fctx, time_ffont, s_config.time_font_size);
        fctx_set_color_bias(&fctx, 0);
        fctx_set_offset(&fctx, time_pos);
        fctx_draw_string(&fctx, timedraw, time_ffont, GTextAlignmentCenter, FTextAnchorMiddle);
        fctx_end_fill(&fctx);
    }

    if(s_settings.ShowInfo){
        fctx_begin_fill(&fctx);
        fctx_set_fill_color(&fctx, s_settings.other_text_color);
        fctx_set_text_em_height(&fctx, time_ffont, s_config.info_font_size);

        int datedraw;
        datedraw = s_day;
        char datenow[3];
        snprintf(datenow, sizeof(datenow), "%02d", datedraw);

        int monthdraw;
        monthdraw = (s_month + 1);
        char monthnow[3];
        snprintf(monthnow, sizeof(monthnow), "%02d", monthdraw);

        int battery_level = battery_state_service_peek().charge_percent;
        char battperc[4];
        snprintf(battperc, sizeof(battperc), "%d", battery_level);

        char information[10];
        snprintf(information, sizeof(information), "%s.%s.%s",datenow,monthnow,battperc);

        FPoint text_pos;
        text_pos.x = INT_TO_FIXED(bounds.size.w / 2 - 27 - 32);
        text_pos.y = INT_TO_FIXED(bounds.size.h / 2 - s_config.info_font_size + 4);

        fctx_set_offset(&fctx, text_pos);
        fctx_draw_string(&fctx, information, time_ffont, GTextAlignmentLeft, FTextAnchorBottom);
        fctx_end_fill(&fctx);
    }

    if(s_settings.ShowText){
        FPoint word_pos;
        fctx_begin_fill(&fctx);
        fctx_set_fill_color(&fctx, s_settings.other_text_color);
        fctx_set_text_em_height(&fctx, time_ffont, s_config.other_text_font_size);

        word_pos.x = INT_TO_FIXED(bounds.size.w / 2 + 27 + 2 + 31);
        word_pos.y = INT_TO_FIXED(bounds.size.h / 2 + s_config.info_font_size + 8 - 3 );

        fctx_set_offset(&fctx, word_pos);
        char textdraw [15];
        snprintf(textdraw, sizeof(textdraw), "%s", s_settings.LogoText);
        fctx_draw_string(&fctx, textdraw, time_ffont, GTextAlignmentRight, FTextAnchorTop);
        fctx_end_fill(&fctx);
    }

    fctx_deinit_context(&fctx);

    if(s_settings.ShowLine){
     graphics_context_set_antialiased(ctx, true);
     graphics_context_set_stroke_width(ctx, 1);
     graphics_context_set_stroke_color(ctx, s_settings.line_color);
     GPoint start = GPoint (bounds.size.w / 2 - 25, bounds.size.h / 2 + 30 + 14);  //PBL_IF_ROUND_ELSE (GPoint (42,66),GPoint (31,62));
     GPoint end = GPoint (bounds.size.w / 2 + 58, bounds.size.h / 2 + 30 + 14); //GPoint end = PBL_IF_ROUND_ELSE (GPoint (89,66),GPoint (71,62));
     graphics_draw_line(ctx, start, end);
     GPoint start2 = GPoint (bounds.size.w / 2 - 25, bounds.size.h / 2 + 30 + 14);  //PBL_IF_ROUND_ELSE (GPoint (42,66),GPoint (31,62));
     GPoint end2 = GPoint (bounds.size.w / 2 - 49 + 7, bounds.size.h / 2 + 54 + 14 - 7); //GPoint end = PBL_IF_ROUND_ELSE (GPoint (89,66),GPoint (71,62));
     graphics_draw_line(ctx, start2, end2);
    }

    // Reload the frame bitmap now that fctx has released its framebuffer
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int min = t->tm_min;
    s_current_frame_index = -1;
    prv_load_frame((min < 30) ? min : (59 - min));
    layer_mark_dirty(s_canvas_layer);
}

static void prv_update_time_text(void) {
    if (s_time_layer) layer_mark_dirty(s_time_layer);
}

// ── Bluetooth and Quiet time icon handling ──────────────────────────────────────────────────────────────

static void quiet_time_icon () {
    layer_set_hidden(s_canvas_qt_icon, !quiet_time_is_active());
    layer_mark_dirty(s_canvas_qt_icon);
}

static void bluetooth_vibe_icon (bool connected) {
   layer_set_hidden(s_canvas_bt_icon, connected);

   layer_mark_dirty(s_canvas_bt_icon);

  if((!connected && !quiet_time_is_active() && s_settings.BTVibeOn)) {
    vibes_double_pulse();
  }
}

static void layer_update_proc_bt(Layer * layer, GContext * ctx){
    GRect bounds = layer_get_bounds(layer);

    GRect BTIconRect =
      GRect(bounds.size.w/2 - 9, bounds.size.h/2 + 3 - 39 - 1 - 19 - 15,18,18);

      graphics_context_set_text_color(ctx, s_settings.other_text_color);
      graphics_context_set_antialiased(ctx, true);
      graphics_draw_text(ctx, "z", FontBTQTIcons, BTIconRect, GTextOverflowModeFill,GTextAlignmentCenter, NULL);

}

static void layer_update_proc_qt(Layer * layer, GContext * ctx){
  GRect bounds = layer_get_bounds(layer);

  GRect QTIconRect =
    GRect(bounds.size.w/2 - 9,bounds.size.h/2 + 3 -39 - 1 - 19,18,18);

  graphics_context_set_text_color(ctx, s_settings.other_text_color);
  graphics_context_set_antialiased(ctx, true);
  graphics_draw_text(ctx, "\U0000E061", FontBTQTIcons, QTIconRect, GTextOverflowModeFill,GTextAlignmentCenter, NULL);

}

// ── Tick handler ──────────────────────────────────────────────────────────────

static void prv_tick_handler(struct tm *t, TimeUnits units) {
    int min = t->tm_min;
    prv_load_frame((min < 30) ? min : (59 - min));
    layer_mark_dirty(s_canvas_layer);

    s_hours = t->tm_hour;
    s_minutes = t->tm_min;
    s_day = t->tm_mday;
    s_month = t->tm_mon;
    prv_update_time_text();
}

// ── AppMessage ────────────────────────────────────────────────────────────────

static void prv_apply_settings(void) {
    prv_rebuild_lut();

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    s_hours = t->tm_hour;
    s_minutes = t->tm_min;
    s_day = t->tm_mday;
    s_month = t->tm_mon;

    // Reload frame immediately with new LUT so display updates at once
    int min = t->tm_min;
    s_current_frame_index = -1;
    prv_load_frame((min < 30) ? min : (59 - min));

    layer_mark_dirty(s_canvas_layer);
    layer_mark_dirty(s_canvas_qt_icon);
    layer_mark_dirty(s_canvas_bt_icon);
    prv_update_time_text();
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
    Tuple *theme_t = dict_find(iter, MESSAGE_KEY_theme);
    if (theme_t) {
        if (theme_t->type == TUPLE_BYTE_ARRAY || theme_t->type == TUPLE_CSTRING) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%s", (char*)theme_t->value->data);
            s_settings.theme = atoi(buf);
        } else {
            s_settings.theme = (int)theme_t->value->int32;
        }
    }

    Tuple *custom_bg_t = dict_find(iter, MESSAGE_KEY_custom_bg);
        if (custom_bg_t) s_settings.custom_bg = GColorFromHEX(custom_bg_t->value->int32);

    Tuple *custom_lg_t = dict_find(iter, MESSAGE_KEY_custom_lg);
        if (custom_lg_t) s_settings.custom_lg = GColorFromHEX(custom_lg_t->value->int32);

    Tuple *custom_dg_t = dict_find(iter, MESSAGE_KEY_custom_dg);
        if (custom_dg_t) s_settings.custom_dg = GColorFromHEX(custom_dg_t->value->int32);

    Tuple *custom_fg_t = dict_find(iter, MESSAGE_KEY_custom_fg);
        if (custom_fg_t) s_settings.custom_fg = GColorFromHEX(custom_fg_t->value->int32);

    Tuple * tm_txt_color = dict_find(iter,MESSAGE_KEY_time_text_color);
        if (tm_txt_color){
        s_settings.time_text_color = GColorFromHEX(tm_txt_color-> value -> int32);
        }

    Tuple * oth_txt_color = dict_find(iter,MESSAGE_KEY_other_text_color);
        if (oth_txt_color){
        s_settings.other_text_color = GColorFromHEX(oth_txt_color-> value -> int32);
        }

    Tuple * ln_color = dict_find(iter,MESSAGE_KEY_line_color);
        if (ln_color){
        s_settings.line_color = GColorFromHEX(ln_color-> value -> int32);
        }

    Tuple * addzero12_t = dict_find(iter, MESSAGE_KEY_AddZero12h);
        if (addzero12_t) {
        s_settings.AddZero12h = addzero12_t->value->int32 == 1;
        }
    Tuple * remzero24_t = dict_find(iter, MESSAGE_KEY_RemoveZero24h);
        if (remzero24_t) {
        s_settings.RemoveZero24h = remzero24_t->value->int32 == 1;
        }
    Tuple * vibe_t = dict_find(iter, MESSAGE_KEY_BTVibeOn);
        if (vibe_t) {
        s_settings.BTVibeOn = vibe_t->value->int32 == 1;
        }
    Tuple *localampm_t = dict_find(iter, MESSAGE_KEY_showlocalAMPM);
        if (localampm_t) {
        s_settings.showlocalAMPM = localampm_t->value->int32 == 1;
        }

    Tuple *logotext_t = dict_find(iter, MESSAGE_KEY_LogoText);
        if (logotext_t && strlen(logotext_t->value->cstring) > 0) {
        // If the custom text field is not blank, use the user's text
        snprintf(s_settings.LogoText, sizeof(s_settings.LogoText), "%s", logotext_t->value->cstring);
        } else {
        // If the custom text field is blank but the logo is enabled, use the default text
        snprintf(s_settings.LogoText, sizeof(s_settings.LogoText), "%s", "DIVERGENCE");
        }
    
    Tuple *showline_t = dict_find(iter, MESSAGE_KEY_ShowLine);
        if (showline_t) {
        s_settings.ShowLine = showline_t->value->int32 == 1;
        }

    Tuple *showtext_t = dict_find(iter, MESSAGE_KEY_ShowText);
        if (showtext_t) {
        s_settings.ShowText = showtext_t->value->int32 == 1;
        }

    Tuple *showtime_t = dict_find(iter, MESSAGE_KEY_ShowTime);
        if (showtime_t) {
        s_settings.ShowTime = showtime_t->value->int32 == 1;
        }

    Tuple *showinfo_t = dict_find(iter, MESSAGE_KEY_ShowInfo);
        if (showinfo_t) {
        s_settings.ShowInfo = showinfo_t->value->int32 == 1;
        }
       

    prv_save_settings();
    prv_apply_settings();
}

// ── Window lifecycle ──────────────────────────────────────────────────────────

static void prv_window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    

    s_canvas_layer = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_canvas_layer, prv_canvas_update);
    layer_add_child(root, s_canvas_layer);

    GRect window_frame = layer_get_frame(root);

    s_time_layer = layer_create(window_frame);
    layer_set_update_proc(s_time_layer, layer_update_proc_time);
    layer_add_child(root, s_time_layer);

    s_canvas_qt_icon = layer_create(window_frame);
       quiet_time_icon();
       layer_set_update_proc(s_canvas_qt_icon, layer_update_proc_qt);
       layer_add_child(root, s_canvas_qt_icon);

    s_canvas_bt_icon = layer_create(window_frame);
      connected = connection_service_peek_pebble_app_connection();
      layer_set_hidden(s_canvas_bt_icon, connected);
      layer_set_update_proc(s_canvas_bt_icon, layer_update_proc_bt);
      layer_add_child(root, s_canvas_bt_icon);

      bluetooth_vibe_icon(connection_service_peek_pebble_app_connection());


    prv_apply_settings();
}

static void prv_window_unload(Window *window) {
    if (s_frame_bitmap) { gbitmap_destroy(s_frame_bitmap); s_frame_bitmap = NULL; }
    layer_destroy(s_time_layer);
    layer_destroy(s_canvas_qt_icon);
    layer_destroy(s_canvas_bt_icon);
    layer_destroy(s_canvas_layer);
}

// ── Init / deinit ─────────────────────────────────────────────────────────────

static void prv_init(void) {
    prv_load_settings();
    prv_rebuild_lut();

    connection_service_subscribe((ConnectionHandlers){
      .pebble_app_connection_handler = bluetooth_vibe_icon
    });

    FontBTQTIcons = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DRIPICONS_12));
    time_ffont = ffont_create_from_resource(RESOURCE_ID_DIN_CONDENSED_FFONT);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "time_ffont=%p", time_ffont);


    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){ .load = prv_window_load, .unload = prv_window_unload });
    window_stack_push(s_window, true);
    tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
    
    app_message_register_inbox_received(prv_inbox_received);
    app_message_open(256, 64);
}

static void prv_deinit(void) {
    tick_timer_service_unsubscribe();
    connection_service_unsubscribe();
    if (time_ffont) ffont_destroy(time_ffont);
    fonts_unload_custom_font(FontBTQTIcons);
    window_destroy(s_window);
}

int main(void) {
    prv_init();
    app_event_loop();
    prv_deinit();
}