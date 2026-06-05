/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/usb.h>
#include <zmk/wpm.h>

#include "status.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
};

struct wpm_status_state {
    uint8_t wpm;
};

static void draw_bluetooth_icon(lv_obj_t *canvas, lv_draw_line_dsc_t *line_dsc) {
    lv_point_t upper[] = {{8, 9}, {14, 15}, {11, 18}, {11, 6}, {14, 9}, {8, 15}};
    canvas_draw_line(canvas, upper, ARRAY_SIZE(upper), line_dsc);
}

static void draw_battery_icon(lv_obj_t *canvas, const struct status_state *state,
                              lv_draw_rect_dsc_t *fg_dsc, lv_draw_rect_dsc_t *bg_dsc) {
    canvas_draw_rect(canvas, 39, 8, 16, 8, fg_dsc);
    canvas_draw_rect(canvas, 40, 9, 14, 6, bg_dsc);
    canvas_draw_rect(canvas, 56, 11, 2, 3, fg_dsc);

    uint8_t fill = MIN((state->battery + 9) / 10, 10);
    if (fill > 0) {
        canvas_draw_rect(canvas, 42, 11, fill, 2, fg_dsc);
    }
}

static bool is_connected(const struct status_state *state) {
    switch (state->selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        return true;
    case ZMK_TRANSPORT_BLE:
        return state->active_profile_bonded && state->active_profile_connected;
    default:
        return false;
    }
}

static void draw_connection_icon(lv_obj_t *canvas, const struct status_state *state,
                                 lv_draw_line_dsc_t *line_dsc, lv_draw_arc_dsc_t *arc_dsc,
                                 lv_draw_rect_dsc_t *fill_dsc) {
    if (is_connected(state)) {
        canvas_draw_rect(canvas, 33, 49, 3, 3, fill_dsc);
        canvas_draw_arc(canvas, 34, 50, 8, 220, 320, arc_dsc);
        canvas_draw_arc(canvas, 34, 50, 13, 220, 320, arc_dsc);
        canvas_draw_arc(canvas, 34, 50, 18, 220, 320, arc_dsc);
        return;
    }

    lv_point_t slash_a[] = {{29, 44}, {39, 54}};
    lv_point_t slash_b[] = {{39, 44}, {29, 54}};
    canvas_draw_line(canvas, slash_a, ARRAY_SIZE(slash_a), line_dsc);
    canvas_draw_line(canvas, slash_b, ARRAY_SIZE(slash_b), line_dsc);
}

static void draw_top(lv_obj_t *widget, const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t right_label_dsc;
    init_label_dsc(&right_label_dsc, LVGL_FOREGROUND, &lv_font_unscii_8, LV_TEXT_ALIGN_RIGHT);
    lv_draw_label_dsc_t center_label_dsc;
    init_label_dsc(&center_label_dsc, LVGL_FOREGROUND, &lv_font_unscii_8, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t profile_label_dsc;
    init_label_dsc(&profile_label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
    lv_draw_rect_dsc_t fill_dsc;
    init_rect_dsc(&fill_dsc, LVGL_FOREGROUND);
    lv_draw_rect_dsc_t bg_dsc;
    init_rect_dsc(&bg_dsc, LVGL_BACKGROUND);
    lv_draw_line_dsc_t line_dsc;
    init_line_dsc(&line_dsc, LVGL_FOREGROUND, 1);
    lv_draw_arc_dsc_t arc_dsc;
    init_arc_dsc(&arc_dsc, LVGL_FOREGROUND, 1);

    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);

    draw_bluetooth_icon(canvas, &line_dsc);

    char profile_text[3] = {};
    if (state->selected_endpoint.transport == ZMK_TRANSPORT_USB) {
        snprintf(profile_text, sizeof(profile_text), "U");
    } else {
        snprintf(profile_text, sizeof(profile_text), "%d", state->active_profile_index + 1);
    }
    canvas_draw_text(canvas, 18, 0, 18, &profile_label_dsc, profile_text);

    draw_battery_icon(canvas, state, &fill_dsc, &bg_dsc);

    char battery_text[5] = {};
    snprintf(battery_text, sizeof(battery_text), "%d%%", state->battery);
    canvas_draw_text(canvas, 35, 19, 26, &right_label_dsc, battery_text);

    draw_connection_icon(canvas, state, &line_dsc, &arc_dsc, &fill_dsc);
    canvas_draw_text(canvas, 0, 57, 68, &center_label_dsc, is_connected(state) ? "ONLINE" : "OFFLINE");

    rotate_canvas(canvas);
}

static void draw_middle(lv_obj_t *widget, const struct status_state *state) {
    ARG_UNUSED(state);

    lv_obj_t *canvas = lv_obj_get_child(widget, 1);

    lv_draw_label_dsc_t name_dsc;
    init_label_dsc(&name_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t wpm_label_dsc;
    init_label_dsc(&wpm_label_dsc, LVGL_FOREGROUND, &lv_font_unscii_8, LV_TEXT_ALIGN_CENTER);
    lv_draw_line_dsc_t line_dsc;
    init_line_dsc(&line_dsc, LVGL_FOREGROUND, 1);

    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);

    canvas_draw_text(canvas, 0, 9, 68, &name_dsc, "Dinda");

    lv_point_t rule[] = {{17, 28}, {51, 28}};
    canvas_draw_line(canvas, rule, ARRAY_SIZE(rule), &line_dsc);

    canvas_draw_text(canvas, 0, 48, 68, &wpm_label_dsc, "WPM");

    rotate_canvas(canvas);
}

static void draw_bottom(lv_obj_t *widget, const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 2);

    lv_draw_label_dsc_t wpm_dsc;
    init_label_dsc(&wpm_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);
    lv_draw_rect_dsc_t fill_dsc;
    init_rect_dsc(&fill_dsc, LVGL_FOREGROUND);

    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);

    char wpm_text[4] = {};
    snprintf(wpm_text, sizeof(wpm_text), "%d", state->wpm);
    canvas_draw_text(canvas, 0, 2, 68, &wpm_dsc, wpm_text);
    canvas_draw_rect(canvas, 31, 34, 6, 1, &fill_dsc);

    rotate_canvas(canvas);
}

static void redraw_all(struct zmk_widget_status *widget) {
    draw_top(widget->obj, &widget->state);
    draw_middle(widget->obj, &widget->state);
    draw_bottom(widget->obj, &widget->state);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif
    widget->state.battery = state.level;
    draw_top(widget->obj, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif

static void set_output_status(struct zmk_widget_status *widget,
                              const struct output_status_state *state) {
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_index = state->active_profile_index;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;
    draw_top(widget->obj, &widget->state);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_output_status(widget, &state); }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    return (struct output_status_state){
        .selected_endpoint = zmk_endpoint_get_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

static void set_wpm_status(struct zmk_widget_status *widget, struct wpm_status_state state) {
    widget->state.wpm = state.wpm;
    draw_bottom(widget->obj, &widget->state);
}

static void wpm_status_update_cb(struct wpm_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_wpm_status(widget, state); }
}

struct wpm_status_state wpm_status_get_state(const zmk_event_t *eh) {
    return (struct wpm_status_state){.wpm = zmk_wpm_get_state()};
};

ZMK_DISPLAY_WIDGET_LISTENER(widget_wpm_status, struct wpm_status_state, wpm_status_update_cb,
                            wpm_status_get_state)
ZMK_SUBSCRIPTION(widget_wpm_status, zmk_wpm_state_changed);

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);

    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    lv_obj_t *middle = lv_canvas_create(widget->obj);
    lv_obj_align(middle, LV_ALIGN_TOP_LEFT, 24, 0);
    lv_canvas_set_buffer(middle, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    lv_obj_t *bottom = lv_canvas_create(widget->obj);
    lv_obj_align(bottom, LV_ALIGN_TOP_LEFT, -44, 0);
    lv_canvas_set_buffer(bottom, widget->cbuf3, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_output_status_init();
    widget_wpm_status_init();

    redraw_all(widget);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
