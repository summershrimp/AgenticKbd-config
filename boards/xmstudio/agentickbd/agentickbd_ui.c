/*
 * Copyright (c) 2026 Hakusai Zhang
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <lvgl.h>

LOG_MODULE_REGISTER(agentickbd_ui, CONFIG_ZMK_LOG_LEVEL);

#define AGENTICKBD_UI_STACK_SIZE 8192
#define AGENTICKBD_UI_PRIORITY 7
#define AGENTICKBD_UI_WIDTH DT_PROP(DT_CHOSEN(zephyr_display), width)
#define AGENTICKBD_UI_HEIGHT DT_PROP(DT_CHOSEN(zephyr_display), height)
#define AGENTICKBD_BACKLIGHT_DEFAULT 0x20

static const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct pwm_dt_spec backlight_pwm =
    PWM_DT_SPEC_GET_BY_IDX(DT_NODELABEL(display_backlight_pwm), 0);
static uint8_t lvgl_buf[AGENTICKBD_UI_WIDTH * AGENTICKBD_UI_HEIGHT * 2] __aligned(4);
static uint16_t black_row[AGENTICKBD_UI_WIDTH];
static uint32_t flush_count;

static int agentickbd_set_backlight(uint8_t brightness) {
    uint32_t pulse;
    int ret;

    if (!pwm_is_ready_dt(&backlight_pwm)) {
        printk("backlight pwm not ready\n");
        LOG_ERR("Backlight PWM device not ready");
        return -ENODEV;
    }

    pulse = ((uint64_t)backlight_pwm.period * brightness) / UINT8_MAX;
    printk("backlight set brightness=%u period=%u pulse=%u flags=0x%x\n", brightness,
           backlight_pwm.period, pulse, backlight_pwm.flags);
    LOG_INF("Backlight set: brightness=%u period=%u pulse=%u flags=0x%x", brightness,
            backlight_pwm.period, pulse, backlight_pwm.flags);

    ret = pwm_set_dt(&backlight_pwm, backlight_pwm.period, pulse);
    printk("backlight pwm_set_dt ret=%d\n", ret);
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
    lv_obj_t *box;
    lv_obj_t *label;

    LOG_INF("build_ui: entered");

    screen = lv_obj_create(NULL);

    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    box = lv_obj_create(screen);

    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, 60, 28);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x20d060), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_center(box);

    label = lv_label_create(box);

    lv_label_set_text(label, "LVGL OK");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);
    lv_obj_update_layout(screen);

    lv_screen_load(screen);

    lv_obj_invalidate(screen);
    LOG_INF("build_ui: forcing first refresh");
    lv_refr_now(display);
}

static void agentickbd_ui_thread(void *p1, void *p2, void *p3) {
    struct display_capabilities caps;
    lv_display_t *display;
    lv_mem_monitor_t mem_mon;
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    printk("agentickbd_ui_thread start\n");

    if (!device_is_ready(display_dev)) {
        printk("display device not ready\n");
        LOG_ERR("Display device not ready");
        return;
    }

    display_get_capabilities(display_dev, &caps);
    printk("display ready %ux%u pf=%u\n", caps.x_resolution, caps.y_resolution,
           caps.current_pixel_format);
    LOG_INF("Display ready: %ux%u pf=%u", caps.x_resolution, caps.y_resolution,
            caps.current_pixel_format);
    printk("backlight pwm dev=%s ch=%u period=%u flags=0x%x ready=%d\n",
           backlight_pwm.dev ? backlight_pwm.dev->name : "<null>", backlight_pwm.channel,
           backlight_pwm.period, backlight_pwm.flags, pwm_is_ready_dt(&backlight_pwm));
    LOG_INF("Backlight pwm dev=%s channel=%u period=%u flags=0x%x ready=%d",
            backlight_pwm.dev ? backlight_pwm.dev->name : "<null>", backlight_pwm.channel,
            backlight_pwm.period, backlight_pwm.flags, pwm_is_ready_dt(&backlight_pwm));

    if (display_blanking_off(display_dev) != 0) {
        printk("display blanking off failed\n");
        LOG_WRN("Display blanking off failed");
    }

    if (agentickbd_clear_display_black(caps.x_resolution, caps.y_resolution) != 0) {
        printk("initial black clear failed\n");
        LOG_WRN("Initial black clear failed");
    }

    if (agentickbd_set_backlight(AGENTICKBD_BACKLIGHT_DEFAULT) != 0) {
        printk("backlight default apply failed\n");
        LOG_WRN("Backlight set failed");
    } else {
        printk("backlight default applied\n");
        LOG_INF("Backlight default brightness applied");
    }

    lv_init();
    lv_tick_set_cb(k_uptime_get_32);
    LOG_INF("LVGL initialized");
    lv_mem_monitor(&mem_mon);
    LOG_INF("LVGL mem: free=%lu used=%lu frag=%u%%", (unsigned long)mem_mon.free_size,
            (unsigned long)mem_mon.total_size - (unsigned long)mem_mon.free_size,
            (unsigned int)mem_mon.frag_pct);

    display = lv_display_create(caps.x_resolution, caps.y_resolution);
    if (display == NULL) {
        LOG_ERR("LVGL display create failed");
        return;
    }
    LOG_INF("LVGL display created");

    lv_display_set_default(display);

    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

    lv_display_set_flush_cb(display, agentickbd_lvgl_flush_cb);
    LOG_INF("LVGL flush callback installed");

    lv_display_set_buffers(display, lvgl_buf, NULL, sizeof(lvgl_buf), LV_DISPLAY_RENDER_MODE_FULL);
    LOG_INF("LVGL buffers installed: %u bytes", (unsigned int)sizeof(lvgl_buf));

    build_ui(display, caps.x_resolution, caps.y_resolution);

    while (1) {
        uint32_t sleep_ms = lv_timer_handler();

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
