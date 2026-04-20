#include <string.h>

#include <raw_hid/events.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "agentickbd_ui.h"

LOG_MODULE_REGISTER(agentickbd_hid, CONFIG_ZMK_LOG_LEVEL);

/*
 * Binary TLV payload format:
 *   [type:1][len:1][value:len]...
 *
 * Supported tags:
 *   0x01 progress percent      len=1   value[0] = 0..100
 *   0x02 progress bar color    len=3   value    = R,G,B
 *   0x03 percent text color    len=3   value    = R,G,B
 *   0x04 top text              len=0..36 raw bytes, truncated to one line
 *   0x05 top text color        len=3   value    = R,G,B
 *   0x06 bottom text           len=0..36 raw bytes, truncated to one line
 *   0x07 bottom text color     len=3   value    = R,G,B
 */
enum agentickbd_hid_tlv_type {
    AGENTICKBD_HID_TLV_PROGRESS = 0x01,
    AGENTICKBD_HID_TLV_BAR_COLOR = 0x02,
    AGENTICKBD_HID_TLV_PERCENT_COLOR = 0x03,
    AGENTICKBD_HID_TLV_TOP_TEXT = 0x04,
    AGENTICKBD_HID_TLV_TOP_COLOR = 0x05,
    AGENTICKBD_HID_TLV_BOTTOM_TEXT = 0x06,
    AGENTICKBD_HID_TLV_BOTTOM_COLOR = 0x07,
};

static uint32_t agentickbd_hid_parse_rgb888(const uint8_t *value) {
    return ((uint32_t)value[0] << 16) | ((uint32_t)value[1] << 8) | (uint32_t)value[2];
}

static void agentickbd_hid_copy_text(char dest[AGENTICKBD_UI_TEXT_MAX_LEN + 1], const uint8_t *value,
                                     uint8_t len) {
    size_t copy_len = MIN((size_t)len, (size_t)AGENTICKBD_UI_TEXT_MAX_LEN);

    memcpy(dest, value, copy_len);
    dest[copy_len] = '\0';
}

static void agentickbd_hid_apply_tlv(uint8_t type, const uint8_t *value, uint8_t len) {
    char text[AGENTICKBD_UI_TEXT_MAX_LEN + 1];

    switch (type) {
    case AGENTICKBD_HID_TLV_PROGRESS:
        if (len != 1U) {
            LOG_WRN("TLV progress length invalid: %u", len);
            return;
        }
        agentickbd_ui_set_progress(value[0]);
        return;

    case AGENTICKBD_HID_TLV_BAR_COLOR:
        if (len != 3U) {
            LOG_WRN("TLV bar color length invalid: %u", len);
            return;
        }
        agentickbd_ui_set_progress_color(agentickbd_hid_parse_rgb888(value));
        return;

    case AGENTICKBD_HID_TLV_PERCENT_COLOR:
        if (len != 3U) {
            LOG_WRN("TLV percent color length invalid: %u", len);
            return;
        }
        agentickbd_ui_set_progress_text_color(agentickbd_hid_parse_rgb888(value));
        return;

    case AGENTICKBD_HID_TLV_TOP_TEXT:
        agentickbd_hid_copy_text(text, value, len);
        agentickbd_ui_set_text(AGENTICKBD_UI_TEXT_TOP, text);
        return;

    case AGENTICKBD_HID_TLV_TOP_COLOR:
        if (len != 3U) {
            LOG_WRN("TLV top color length invalid: %u", len);
            return;
        }
        agentickbd_ui_set_text_color(AGENTICKBD_UI_TEXT_TOP, agentickbd_hid_parse_rgb888(value));
        return;

    case AGENTICKBD_HID_TLV_BOTTOM_TEXT:
        agentickbd_hid_copy_text(text, value, len);
        agentickbd_ui_set_text(AGENTICKBD_UI_TEXT_BOTTOM, text);
        return;

    case AGENTICKBD_HID_TLV_BOTTOM_COLOR:
        if (len != 3U) {
            LOG_WRN("TLV bottom color length invalid: %u", len);
            return;
        }
        agentickbd_ui_set_text_color(AGENTICKBD_UI_TEXT_BOTTOM,
                                     agentickbd_hid_parse_rgb888(value));
        return;

    default:
        LOG_WRN("Unknown TLV type: 0x%02x", type);
        return;
    }
}

static void handle_hid_payload(const uint8_t *data, size_t len) {
    size_t offset = 0U;

    while ((offset + 2U) <= len) {
        uint8_t type = data[offset];
        uint8_t value_len = data[offset + 1U];
        size_t next = offset + 2U + value_len;

        if (next > len) {
            LOG_WRN("Truncated TLV: type=0x%02x len=%u remaining=%u", type, value_len,
                    (unsigned int)(len - offset - 2U));
            return;
        }

        agentickbd_hid_apply_tlv(type, &data[offset + 2U], value_len);
        offset = next;
    }

    if (offset != len) {
        LOG_WRN("Dangling HID byte count: %u", (unsigned int)(len - offset));
    }
}

static int raw_hid_received_event_listener(const zmk_event_t *eh) {
    struct raw_hid_received_event *event = as_raw_hid_received_event(eh);

    if (event == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    LOG_INF("Received %u bytes of HID UI TLV data", event->length);
    handle_hid_payload(event->data, event->length);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(process_raw_hid_event, raw_hid_received_event_listener);
ZMK_SUBSCRIPTION(process_raw_hid_event, raw_hid_received_event);
