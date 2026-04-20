/*
 * Copyright (c) 2026 Hakusai Zhang
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <lvgl.h>

#include "agentickbd_ui.h"

LOG_MODULE_REGISTER(agentickbd_ui, CONFIG_ZMK_LOG_LEVEL);

#define AGENTICKBD_UI_STACK_SIZE 8192
#define AGENTICKBD_UI_PRIORITY 7
#define AGENTICKBD_UI_WIDTH DT_PROP(DT_CHOSEN(zephyr_display), width)
#define AGENTICKBD_UI_HEIGHT DT_PROP(DT_CHOSEN(zephyr_display), height)
#define AGENTICKBD_BACKLIGHT_DEFAULT 0x20
#define AGENTICKBD_PROGRESS_DEFAULT_COLOR 0x00c853
#define AGENTICKBD_PROGRESS_TEXT_DEFAULT_COLOR 0xffffff
#define AGENTICKBD_TOP_TEXT_DEFAULT_COLOR 0xf4f7fb
#define AGENTICKBD_BOTTOM_TEXT_DEFAULT_COLOR 0x9aa7b8
#define AGENTICKBD_TRACK_COLOR 0x26313f

struct agentickbd_ui_state {
    uint8_t progress_percent;
    uint32_t progress_color;
    uint32_t progress_text_color;
    uint32_t text_color[2];
    char text[2][AGENTICKBD_UI_TEXT_MAX_LEN + 1];
    bool dirty;
};

static const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct pwm_dt_spec backlight_pwm =
    PWM_DT_SPEC_GET_BY_IDX(DT_NODELABEL(display_backlight_pwm), 0);
static uint8_t lvgl_buf[AGENTICKBD_UI_WIDTH * AGENTICKBD_UI_HEIGHT * 2] __aligned(4);
static uint16_t black_row[AGENTICKBD_UI_WIDTH];
static uint32_t flush_count;
static struct k_mutex ui_state_mutex;
static struct agentickbd_ui_state ui_state = {
    .progress_percent = 50U,
    .progress_color = AGENTICKBD_PROGRESS_DEFAULT_COLOR,
    .progress_text_color = AGENTICKBD_PROGRESS_TEXT_DEFAULT_COLOR,
    .text_color =
        {
            AGENTICKBD_TOP_TEXT_DEFAULT_COLOR,
            AGENTICKBD_BOTTOM_TEXT_DEFAULT_COLOR,
        },
    .text =
        {
            "Waiting for host command",
            "Use HID to update text and progress",
        },
    .dirty = true,
};
static lv_obj_t *top_label;
static lv_obj_t *bottom_label;
static lv_obj_t *progress_bar;

static lv_color_t agentickbd_ui_color(uint32_t rgb888) { return lv_color_hex(rgb888 & 0x00ffffffU); }

bool agentickbd_ui_parse_color(const char *value, uint32_t *rgb888) {
    unsigned int parsed;
    int ret;

    if (value == NULL || rgb888 == NULL) {
        return false;
    }

    if (value[0] == '#') {
        value++;
    }

    if (strlen(value) != 6U) {
        return false;
    }

    ret = sscanf(value, "%06x", &parsed);
    if (ret != 1) {
        return false;
    }

    *rgb888 = parsed & 0x00ffffffU;
    return true;
}

static void agentickbd_ui_copy_text(char dest[AGENTICKBD_UI_TEXT_MAX_LEN + 1], const char *src) {
    size_t copy_len;

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    copy_len = strnlen(src, AGENTICKBD_UI_TEXT_MAX_LEN);
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

void agentickbd_ui_set_progress(uint8_t percent) {
    k_mutex_lock(&ui_state_mutex, K_FOREVER);
    ui_state.progress_percent = MIN(percent, 100U);
    ui_state.dirty = true;
    k_mutex_unlock(&ui_state_mutex);
}

void agentickbd_ui_set_progress_color(uint32_t rgb888) {
    k_mutex_lock(&ui_state_mutex, K_FOREVER);
    ui_state.progress_color = rgb888 & 0x00ffffffU;
    ui_state.dirty = true;
    k_mutex_unlock(&ui_state_mutex);
}

void agentickbd_ui_set_progress_text_color(uint32_t rgb888) {
    k_mutex_lock(&ui_state_mutex, K_FOREVER);
    ui_state.progress_text_color = rgb888 & 0x00ffffffU;
    ui_state.dirty = true;
    k_mutex_unlock(&ui_state_mutex);
}

void agentickbd_ui_set_text(enum agentickbd_ui_text_slot slot, const char *text) {
    if (slot != AGENTICKBD_UI_TEXT_TOP && slot != AGENTICKBD_UI_TEXT_BOTTOM) {
        return;
    }

    k_mutex_lock(&ui_state_mutex, K_FOREVER);
    agentickbd_ui_copy_text(ui_state.text[slot], text);
    ui_state.dirty = true;
    k_mutex_unlock(&ui_state_mutex);
}

void agentickbd_ui_set_text_color(enum agentickbd_ui_text_slot slot, uint32_t rgb888) {
    if (slot != AGENTICKBD_UI_TEXT_TOP && slot != AGENTICKBD_UI_TEXT_BOTTOM) {
        return;
    }

    k_mutex_lock(&ui_state_mutex, K_FOREVER);
    ui_state.text_color[slot] = rgb888 & 0x00ffffffU;
    ui_state.dirty = true;
    k_mutex_unlock(&ui_state_mutex);
}

static void agentickbd_ui_apply_pending_state(void) {
    struct agentickbd_ui_state snapshot;

    if (top_label == NULL || bottom_label == NULL || progress_bar == NULL) {
        return;
    }

    k_mutex_lock(&ui_state_mutex, K_FOREVER);
    if (!ui_state.dirty) {
        k_mutex_unlock(&ui_state_mutex);
        return;
    }

    snapshot = ui_state;
    ui_state.dirty = false;
    k_mutex_unlock(&ui_state_mutex);

    lv_label_set_text(top_label, snapshot.text[AGENTICKBD_UI_TEXT_TOP]);
    lv_obj_set_style_text_color(top_label,
                                agentickbd_ui_color(snapshot.text_color[AGENTICKBD_UI_TEXT_TOP]), 0);

    lv_label_set_text(bottom_label, snapshot.text[AGENTICKBD_UI_TEXT_BOTTOM]);
    lv_obj_set_style_text_color(
        bottom_label, agentickbd_ui_color(snapshot.text_color[AGENTICKBD_UI_TEXT_BOTTOM]), 0);

    lv_bar_set_value(progress_bar, snapshot.progress_percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress_bar, agentickbd_ui_color(snapshot.progress_color),
                              LV_PART_INDICATOR);
}

static int agentickbd_set_backlight(uint8_t brightness) {
    uint32_t pulse;
    int ret;

    if (!pwm_is_ready_dt(&backlight_pwm)) {
        LOG_ERR("Backlight PWM device not ready");
        return -ENODEV;
    }

    pulse = ((uint64_t)backlight_pwm.period * brightness) / UINT8_MAX;
    ret = pwm_set_dt(&backlight_pwm, backlight_pwm.period, pulse);
    LOG_INF("Backlight pwm_set_dt ret=%d", ret);

    return ret;
}

static int agentickbd_clear_display_black(uint16_t width, uint16_t height) {
    struct display_buffer_descriptor desc = {
        .buf_size = width * sizeof(uint16_t),
        .width = width,
        .pitch = width,
        .height = 1,
        .frame_incomplete = true,
    };

    memset(black_row, 0, sizeof(black_row));

    for (uint16_t y = 0; y < height; y++) {
        desc.frame_incomplete = (y + 1U) < height;
        if (display_write(display_dev, 0, y, &desc, black_row) != 0) {
            return -EIO;
        }
    }

    return 0;
}

static void agentickbd_lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map);

static void build_ui(lv_display_t *display, uint16_t width, uint16_t height) {
    lv_obj_t *screen;
    int32_t bar_width = width;
    int32_t bar_height = 8;
    int32_t bar_y = (int32_t)(height / 2U) - (bar_height / 2);
    int32_t top_area_center_y = bar_y / 2;
    int32_t bottom_area_top = bar_y + bar_height;
    int32_t bottom_area_height = (int32_t)height - bottom_area_top;
    int32_t bottom_area_center_y = bottom_area_top + (bottom_area_height / 2);

    screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    top_label = lv_label_create(screen);
    lv_obj_set_width(top_label, width);
    lv_label_set_long_mode(top_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(top_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(top_label, &lv_font_montserrat_14, 0);
    lv_obj_align(top_label, LV_ALIGN_TOP_MID, 0, top_area_center_y - 8);

    progress_bar = lv_bar_create(screen);
    lv_obj_set_size(progress_bar, bar_width, bar_height);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_obj_set_style_radius(progress_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(progress_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(progress_bar, agentickbd_ui_color(AGENTICKBD_TRACK_COLOR), 0);
    lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(progress_bar, 0, 0);
    lv_obj_set_style_pad_all(progress_bar, 0, 0);
    lv_obj_set_pos(progress_bar, 0, bar_y);

    bottom_label = lv_label_create(screen);
    lv_obj_set_width(bottom_label, width);
    lv_label_set_long_mode(bottom_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(bottom_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(bottom_label, &lv_font_montserrat_14, 0);
    lv_obj_align(bottom_label, LV_ALIGN_TOP_MID, 0, bottom_area_center_y - 8);

    agentickbd_ui_apply_pending_state();
    lv_screen_load(screen);
    lv_obj_update_layout(screen);
    lv_obj_invalidate(screen);
    lv_refr_now(display);
}

static void agentickbd_ui_thread(void *p1, void *p2, void *p3) {
    struct display_capabilities caps;
    lv_display_t *display;
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    k_mutex_init(&ui_state_mutex);

    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device not ready");
        return;
    }

    display_get_capabilities(display_dev, &caps);
    LOG_INF("Display ready: %ux%u pf=%u", caps.x_resolution, caps.y_resolution,
            caps.current_pixel_format);

    if (display_blanking_off(display_dev) != 0) {
        LOG_WRN("Display blanking off failed");
    }

    if (agentickbd_clear_display_black(caps.x_resolution, caps.y_resolution) != 0) {
        LOG_WRN("Initial black clear failed");
    }

    if (agentickbd_set_backlight(AGENTICKBD_BACKLIGHT_DEFAULT) != 0) {
        LOG_WRN("Backlight set failed");
    }

    lv_init();
    lv_tick_set_cb(k_uptime_get_32);

    display = lv_display_create(caps.x_resolution, caps.y_resolution);
    if (display == NULL) {
        LOG_ERR("LVGL display create failed");
        return;
    }

    lv_display_set_default(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, agentickbd_lvgl_flush_cb);
    lv_display_set_buffers(display, lvgl_buf, NULL, sizeof(lvgl_buf), LV_DISPLAY_RENDER_MODE_FULL);

    build_ui(display, caps.x_resolution, caps.y_resolution);

    while (1) {
        uint32_t sleep_ms = lv_timer_handler();

        agentickbd_ui_apply_pending_state();

        if (sleep_ms < 5U) {
            sleep_ms = 5U;
        } else if (sleep_ms > 50U) {
            sleep_ms = 50U;
        }

        k_sleep(K_MSEC(sleep_ms));
    }
}

static void agentickbd_lvgl_flush_cb(lv_display_t *display, const lv_area_t *area,
                                     uint8_t *px_map) {
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;
    struct display_buffer_descriptor desc;
    uint8_t *row = px_map;

    flush_count++;
    if (flush_count <= 4U) {
        LOG_INF("flush #%u area=(%d,%d)-(%d,%d)", (unsigned int)flush_count, area->x1, area->y1,
                area->x2, area->y2);
    }

    desc.buf_size = w * 2U;
    desc.width = w;
    desc.pitch = w;
    desc.height = 1;

    for (uint16_t y = 0; y < h; y++) {
        desc.frame_incomplete = ((y + 1U) < h) || !lv_display_flush_is_last(display);
        display_write(display_dev, area->x1, area->y1 + y, &desc, row);
        row += w * 2U;
    }

    lv_display_flush_ready(display);
}

K_THREAD_DEFINE(agentickbd_ui_tid, AGENTICKBD_UI_STACK_SIZE, agentickbd_ui_thread, NULL, NULL,
                NULL, AGENTICKBD_UI_PRIORITY, 0, 0);
