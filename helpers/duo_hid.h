#pragma once
#include <furi_hal.h>
bool duo_hid_is_connected(void);
uint8_t duo_hid_get_led_state(void);
void duo_hid_set_state_callback(HidStateCallback cb, void* ctx);
bool duo_hid_kb_press(uint16_t button);
bool duo_hid_kb_release(uint16_t button);
bool duo_hid_kb_release_all(void);
bool duo_hid_mouse_move(int8_t dx, int8_t dy);
bool duo_hid_mouse_press(uint8_t button);
bool duo_hid_mouse_release(uint8_t button);
bool duo_hid_mouse_scroll(int8_t delta);
bool duo_hid_consumer_key_press(uint16_t button);
bool duo_hid_consumer_key_release(uint16_t button);
bool duo_hid_consumer_key_release_all(void);
