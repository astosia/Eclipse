#include <pebble.h>

static Window *s_window;
static Layer *s_canvas_layer;
static GBitmap *s_frame_bitmap;
static bool s_inverted = true;

#define FRAME_WIDTH 260
#define FRAME_HEIGHT 260
#define TOTAL_FRAMES 30
#define KEY_INVERTED 1

typedef struct {
    int ring_offset_x;
    int ring_offset_y;
} UIConfig;

#ifdef PBL_PLATFORM_EMERY
static const UIConfig config = {
    .ring_offset_x = -30,
    .ring_offset_y = -16
};
#elif defined(PBL_PLATFORM_GABBRO)
static const UIConfig config = {
    .ring_offset_x = 0,
    .ring_offset_y = 0
};
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

static void invert_bitmap(GBitmap *bitmap) {
    GRect bounds = gbitmap_get_bounds(bitmap);
    uint8_t *data = gbitmap_get_data(bitmap);
    int bytes_per_row = gbitmap_get_bytes_per_row(bitmap);

    for (int y = 0; y < bounds.size.h; y++) {
        for (int x = 0; x < bytes_per_row; x++) {
            data[y * bytes_per_row + x] ^= 0xFF;
        }
    }
}

static void update_frame(int frame_index) {
    if (s_frame_bitmap) {
        gbitmap_destroy(s_frame_bitmap);
        s_frame_bitmap = NULL;
    }
    s_frame_bitmap = gbitmap_create_with_resource(s_frame_resources[frame_index]);
    if (s_inverted) {
        invert_bitmap(s_frame_bitmap);
    }
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    if (s_frame_bitmap) {
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, s_frame_bitmap, GRect(config.ring_offset_x, config.ring_offset_y, FRAME_WIDTH, FRAME_HEIGHT));
    }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    int sec = tick_time->tm_sec;
    int frame_index = (sec < 30) ? sec : (59 - sec);
    update_frame(frame_index);
    layer_mark_dirty(s_canvas_layer);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
    Tuple *invert_t = dict_find(iter, MESSAGE_KEY_inverted);
    if (invert_t) {
        s_inverted = (bool)invert_t->value->int32;
        persist_write_bool(KEY_INVERTED, s_inverted);
        // Redraw current frame with new invert setting
        int sec = 0;
        struct tm *t = NULL;
        time_t now = time(NULL);
        t = localtime(&now);
        sec = t->tm_sec;
        int frame_index = (sec < 30) ? sec : (59 - sec);
        update_frame(frame_index);
        layer_mark_dirty(s_canvas_layer);
    }
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(window_layer, s_canvas_layer);

    update_frame(0);
}

static void window_unload(Window *window) {
    if (s_frame_bitmap) {
        gbitmap_destroy(s_frame_bitmap);
        s_frame_bitmap = NULL;
    }
    layer_destroy(s_canvas_layer);
}

static void init(void) {
    s_inverted = persist_read_bool(KEY_INVERTED);

    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);
    tick_timer_service_subscribe(SECOND_UNIT, tick_handler);

    app_message_register_inbox_received(inbox_received_handler);
    app_message_open(64, 64);
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}