#ifndef AGENTICKBD_UI_H
#define AGENTICKBD_UI_H

#include <stdbool.h>
#include <stdint.h>

#define AGENTICKBD_UI_TEXT_MAX_LEN 36

enum agentickbd_ui_text_slot {
    AGENTICKBD_UI_TEXT_TOP = 0,
    AGENTICKBD_UI_TEXT_BOTTOM,
};

void agentickbd_ui_set_progress(uint8_t percent);
void agentickbd_ui_set_progress_color(uint32_t rgb888);
void agentickbd_ui_set_progress_text_color(uint32_t rgb888);
void agentickbd_ui_set_text(enum agentickbd_ui_text_slot slot, const char *text);
void agentickbd_ui_set_text_color(enum agentickbd_ui_text_slot slot, uint32_t rgb888);
bool agentickbd_ui_parse_color(const char *value, uint32_t *rgb888);

#endif /* AGENTICKBD_UI_H */
