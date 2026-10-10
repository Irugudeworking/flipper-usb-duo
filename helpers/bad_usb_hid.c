#include "bad_usb_hid.h"
#include "duo_hid.h"
void* hid_usb_init(FuriHalUsbHidConfig* hid_cfg) {
    UNUSED(hid_cfg);
    return NULL;
}

void hid_usb_deinit(void* inst) {
    UNUSED(inst);

}

void hid_usb_set_state_callback(void* inst, HidStateCallback cb, void* context) {
    UNUSED(inst);
    duo_hid_set_state_callback(cb, context);
}

bool hid_usb_is_connected(void* inst) {
    UNUSED(inst);
    return duo_hid_is_connected();
}

bool hid_usb_kb_press(void* inst, uint16_t button) {
    UNUSED(inst);
    return duo_hid_kb_press(button);
}

bool hid_usb_kb_release(void* inst, uint16_t button) {
    UNUSED(inst);
    return duo_hid_kb_release(button);
}

bool hid_usb_mouse_press(void* inst, uint8_t button) {
    UNUSED(inst);
    return duo_hid_mouse_press(button);
}

bool hid_usb_mouse_release(void* inst, uint8_t button) {
    UNUSED(inst);
    return duo_hid_mouse_release(button);
}

bool hid_usb_mouse_scroll(void* inst, int8_t delta) {
    UNUSED(inst);
    return duo_hid_mouse_scroll(delta);
}

bool hid_usb_mouse_move(void* inst, int8_t dx, int8_t dy) {
    UNUSED(inst);
    return duo_hid_mouse_move(dx, dy);
}

bool hid_usb_mouse_release_all(void* inst) {
    UNUSED(inst);
    return duo_hid_mouse_release(0xFF);
}

bool hid_usb_consumer_press(void* inst, uint16_t button) {
    UNUSED(inst);
    return duo_hid_consumer_key_press(button);
}

bool hid_usb_consumer_release(void* inst, uint16_t button) {
    UNUSED(inst);
    return duo_hid_consumer_key_release(button);
}

bool hid_usb_release_all(void* inst) {
    UNUSED(inst);
    bool state = duo_hid_kb_release_all();
    state &= duo_hid_consumer_key_release_all();
    state &= hid_usb_mouse_release_all(inst);
    return state;
}

uint8_t hid_usb_get_led_state(void* inst) {
    UNUSED(inst);
    return duo_hid_get_led_state();
}

static const BadUsbHidApi hid_api_usb = {
    .init = hid_usb_init,
    .deinit = hid_usb_deinit,
    .set_state_callback = hid_usb_set_state_callback,
    .is_connected = hid_usb_is_connected,

    .kb_press = hid_usb_kb_press,
    .kb_release = hid_usb_kb_release,
    .mouse_press = hid_usb_mouse_press,
    .mouse_release = hid_usb_mouse_release,
    .mouse_scroll = hid_usb_mouse_scroll,
    .mouse_move = hid_usb_mouse_move,
    .consumer_press = hid_usb_consumer_press,
    .consumer_release = hid_usb_consumer_release,
    .release_all = hid_usb_release_all,
    .get_led_state = hid_usb_get_led_state,
};


const BadUsbHidApi* bad_usb_hid_get_interface(BadUsbHidInterface interface) {
 UNUSED(interface); return &hid_api_usb;
}
