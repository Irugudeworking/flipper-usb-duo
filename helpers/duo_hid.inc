#include "usb_hid.h"
#define HID_EP_IN  0x83
#define HID_EP_OUT 0x03
#define HID_EP_SZ  0x10

#define HID_INTERVAL 2

#define HID_VID_DEFAULT 0x046D
#define HID_PID_DEFAULT 0xC529

struct HidIntfDescriptor {
    struct usb_interface_descriptor hid;
    struct usb_hid_descriptor hid_desc;
    struct usb_endpoint_descriptor hid_ep_in;
    struct usb_endpoint_descriptor hid_ep_out;
};

struct HidConfigDescriptor {
    struct usb_config_descriptor config;
    struct HidIntfDescriptor intf_0;
} FURI_PACKED;

enum HidReportId {
    ReportIdKeyboard = 1,
    ReportIdMouse = 2,
    ReportIdConsumer = 3,
};

/* HID report descriptor: keyboard + mouse + consumer control */
static const uint8_t hid_report_desc[] = {
    // clang-format off
    HID_USAGE_PAGE(HID_PAGE_DESKTOP),
    HID_USAGE(HID_DESKTOP_KEYBOARD),
    HID_COLLECTION(HID_APPLICATION_COLLECTION),
        HID_REPORT_ID(ReportIdKeyboard), 
        // Keyboard report
        HID_USAGE_PAGE(HID_DESKTOP_KEYPAD),
        HID_USAGE_MINIMUM(HID_KEYBOARD_L_CTRL),
        HID_USAGE_MAXIMUM(HID_KEYBOARD_R_GUI),
        HID_LOGICAL_MINIMUM(0),
        HID_LOGICAL_MAXIMUM(1),
        HID_REPORT_SIZE(1),
        HID_REPORT_COUNT(8),
        // Input - Modifier keys byte
        HID_INPUT(HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),
        
        HID_REPORT_COUNT(1),
        HID_REPORT_SIZE(8),
        // Input - Reserved byte
        HID_INPUT(HID_IOF_CONSTANT | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),

        HID_USAGE_PAGE(HID_PAGE_LED),
        HID_REPORT_COUNT(8),
        HID_REPORT_SIZE(1),
        HID_USAGE_MINIMUM(1),
        HID_USAGE_MAXIMUM(8),
        // Output - LEDs
        HID_OUTPUT(HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),

        HID_REPORT_COUNT(HID_KB_MAX_KEYS),
        HID_REPORT_SIZE(8),
        HID_LOGICAL_MINIMUM(0),
        HID_RI_LOGICAL_MAXIMUM(16, 255),
        HID_USAGE_PAGE(HID_DESKTOP_KEYPAD),
        HID_USAGE_MINIMUM(0),
        HID_RI_USAGE_MAXIMUM(16, 255),
        // Input - Key codes
        HID_INPUT(HID_IOF_DATA | HID_IOF_ARRAY | HID_IOF_ABSOLUTE),
    HID_END_COLLECTION,

    HID_USAGE_PAGE(HID_PAGE_DESKTOP),
    HID_USAGE(HID_DESKTOP_MOUSE),
    HID_COLLECTION(HID_APPLICATION_COLLECTION),
        HID_USAGE(HID_DESKTOP_POINTER),
        HID_COLLECTION(HID_PHYSICAL_COLLECTION),
            HID_REPORT_ID(ReportIdMouse),
            // Mouse report
            HID_USAGE_PAGE(HID_PAGE_BUTTON),
            HID_USAGE_MINIMUM(1),
            HID_USAGE_MAXIMUM(3),
            HID_LOGICAL_MINIMUM(0),
            HID_LOGICAL_MAXIMUM(1),
            HID_REPORT_COUNT(3),
            HID_REPORT_SIZE(1),
            // Input - Mouse keys
            HID_INPUT(HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),

            HID_REPORT_SIZE(1),
            HID_REPORT_COUNT(5),
            // Input - Mouse keys padding
            HID_INPUT(HID_IOF_CONSTANT | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),
            
            HID_USAGE_PAGE(HID_PAGE_DESKTOP),
            HID_USAGE(HID_DESKTOP_X),
            HID_USAGE(HID_DESKTOP_Y),
            HID_USAGE(HID_DESKTOP_WHEEL),
            HID_LOGICAL_MINIMUM(-127),
            HID_LOGICAL_MAXIMUM(127),
            HID_REPORT_SIZE(8),
            HID_REPORT_COUNT(3),
            // Input - Mouse movement data (x, y, scroll)
            HID_INPUT(HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_RELATIVE),
        HID_END_COLLECTION,
    HID_END_COLLECTION,

    HID_USAGE_PAGE(HID_PAGE_CONSUMER),
    HID_USAGE(HID_CONSUMER_CONTROL),
    HID_COLLECTION(HID_APPLICATION_COLLECTION),
        HID_REPORT_ID(ReportIdConsumer),
        // Consumer report
        HID_LOGICAL_MINIMUM(0),
        HID_RI_LOGICAL_MAXIMUM(16, 0x3FF),
        HID_USAGE_MINIMUM(0),
        HID_RI_USAGE_MAXIMUM(16, 0x3FF),
        HID_REPORT_COUNT(HID_CONSUMER_MAX_KEYS),
        HID_REPORT_SIZE(16),
        // Input - Consumer control keys
        HID_INPUT(HID_IOF_DATA | HID_IOF_ARRAY | HID_IOF_ABSOLUTE),
    HID_END_COLLECTION,
    // clang-format on
};

static const struct usb_hid_descriptor duo_hid_descriptor = {
 .bLength=sizeof(struct usb_hid_descriptor), .bDescriptorType=USB_DTYPE_HID,
 .bcdHID=VERSION_BCD(1,0,0), .bCountryCode=USB_HID_COUNTRY_NONE,
 .bNumDescriptors=1, .bDescriptorType0=USB_DTYPE_HID_REPORT,
 .wDescriptorLength0=sizeof(hid_report_desc),
};
struct HidReportMouse {
    uint8_t report_id;
    uint8_t btn;
    int8_t x;
    int8_t y;
    int8_t wheel;
} FURI_PACKED;

struct HidReportKB {
    uint8_t report_id;
    struct {
        uint8_t mods;
        uint8_t reserved;
        uint8_t btn[HID_KB_MAX_KEYS];
    } boot;
} FURI_PACKED;

struct HidReportConsumer {
    uint8_t report_id;
    uint16_t btn[HID_CONSUMER_MAX_KEYS];
} FURI_PACKED;

struct HidReportLED {
    uint8_t report_id;
    uint8_t led_state;
} FURI_PACKED;

static struct HidReport {
    struct HidReportKB keyboard;
    struct HidReportMouse mouse;
    struct HidReportConsumer consumer;
} FURI_PACKED hid_report;

static bool hid_send_report(uint8_t report_id);
static usbd_respond hid_ep_config(usbd_device* dev, uint8_t cfg);
static usbd_respond hid_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback);
static usbd_device* usb_dev;
static FuriSemaphore* hid_semaphore = NULL;
static bool hid_connected = false;
static HidStateCallback callback;
static void* cb_ctx;
static uint8_t led_state;
static bool boot_protocol = false;

bool duo_hid_is_connected(void) {
    return hid_connected;
}

uint8_t duo_hid_get_led_state(void) {
    return led_state;
}

void duo_hid_set_state_callback(HidStateCallback cb, void* ctx) {
    if(callback != NULL) {
        if(hid_connected == true) callback(false, cb_ctx);
    }

    callback = cb;
    cb_ctx = ctx;

    if(callback != NULL) {
        if(hid_connected == true) callback(true, cb_ctx);
    }
}

bool duo_hid_kb_press(uint16_t button) {
    for(uint8_t key_nb = 0; key_nb < HID_KB_MAX_KEYS; key_nb++) {
        if(hid_report.keyboard.boot.btn[key_nb] == 0) {
            hid_report.keyboard.boot.btn[key_nb] = button & 0xFF;
            break;
        }
    }
    hid_report.keyboard.boot.mods |= (button >> 8);
    return hid_send_report(ReportIdKeyboard);
}

bool duo_hid_kb_release(uint16_t button) {
    for(uint8_t key_nb = 0; key_nb < HID_KB_MAX_KEYS; key_nb++) {
        if(hid_report.keyboard.boot.btn[key_nb] == (button & 0xFF)) {
            hid_report.keyboard.boot.btn[key_nb] = 0;
            break;
        }
    }
    hid_report.keyboard.boot.mods &= ~(button >> 8);
    return hid_send_report(ReportIdKeyboard);
}

bool duo_hid_kb_release_all(void) {
    for(uint8_t key_nb = 0; key_nb < HID_KB_MAX_KEYS; key_nb++) {
        hid_report.keyboard.boot.btn[key_nb] = 0;
    }
    hid_report.keyboard.boot.mods = 0;
    return hid_send_report(ReportIdKeyboard);
}

bool duo_hid_mouse_move(int8_t dx, int8_t dy) {
    hid_report.mouse.x = dx;
    hid_report.mouse.y = dy;
    bool state = hid_send_report(ReportIdMouse);
    hid_report.mouse.x = 0;
    hid_report.mouse.y = 0;
    return state;
}

bool duo_hid_mouse_press(uint8_t button) {
    hid_report.mouse.btn |= button;
    return hid_send_report(ReportIdMouse);
}

bool duo_hid_mouse_release(uint8_t button) {
    hid_report.mouse.btn &= ~button;
    return hid_send_report(ReportIdMouse);
}

bool duo_hid_mouse_scroll(int8_t delta) {
    hid_report.mouse.wheel = delta;
    bool state = hid_send_report(ReportIdMouse);
    hid_report.mouse.wheel = 0;
    return state;
}

bool duo_hid_consumer_key_press(uint16_t button) {
    for(uint8_t key_nb = 0; key_nb < HID_CONSUMER_MAX_KEYS; key_nb++) {
        if(hid_report.consumer.btn[key_nb] == 0) {
            hid_report.consumer.btn[key_nb] = button;
            break;
        }
    }
    return hid_send_report(ReportIdConsumer);
}

bool duo_hid_consumer_key_release(uint16_t button) {
    for(uint8_t key_nb = 0; key_nb < HID_CONSUMER_MAX_KEYS; key_nb++) {
        if(hid_report.consumer.btn[key_nb] == button) {
            hid_report.consumer.btn[key_nb] = 0;
            break;
        }
    }
    return hid_send_report(ReportIdConsumer);
}

bool duo_hid_consumer_key_release_all(void) {
    for(uint8_t key_nb = 0; key_nb < HID_CONSUMER_MAX_KEYS; key_nb++) {
        hid_report.consumer.btn[key_nb] = 0;
    }
    return hid_send_report(ReportIdConsumer);
}

static void duo_hid_init(usbd_device* dev) {
    usb_dev = dev;
    hid_semaphore = furi_semaphore_alloc(1, 1);
    memset(&hid_report, 0, sizeof(hid_report));
    hid_report.keyboard.report_id = ReportIdKeyboard;
    hid_report.mouse.report_id = ReportIdMouse;
    hid_report.consumer.report_id = ReportIdConsumer;
    hid_connected = false; boot_protocol = false; led_state = 0;
    callback = NULL; cb_ctx = NULL;
}
static void duo_hid_free(void) {
    furi_semaphore_free(hid_semaphore);
    hid_semaphore = NULL; usb_dev = NULL;
}
static void hid_on_wakeup(usbd_device* dev) {
    UNUSED(dev);
    if(!hid_connected) {
        hid_connected = true;
        if(callback != NULL) {
            callback(true, cb_ctx);
        }
    }
}

static void hid_on_suspend(usbd_device* dev) {
    UNUSED(dev);
    if(hid_connected) {
        hid_connected = false;
        furi_semaphore_release(hid_semaphore);
        if(callback != NULL) {
            callback(false, cb_ctx);
        }
    }
}

static bool hid_send_report(uint8_t report_id) {
    if((hid_semaphore == NULL) || (hid_connected == false)) return false;
    if((boot_protocol == true) && (report_id != ReportIdKeyboard)) return false;

    FuriStatus status = furi_semaphore_acquire(hid_semaphore, 100);
    if(status == FuriStatusErrorTimeout) {
        return false;
    }
    furi_check(status == FuriStatusOk);
    if(hid_connected == false) {
        return false;
    }
    if(boot_protocol == true) {
        usbd_ep_write(
            usb_dev, HID_EP_IN, &hid_report.keyboard.boot, sizeof(hid_report.keyboard.boot));
    } else {
        if(report_id == ReportIdKeyboard)
            usbd_ep_write(usb_dev, HID_EP_IN, &hid_report.keyboard, sizeof(hid_report.keyboard));
        else if(report_id == ReportIdMouse)
            usbd_ep_write(usb_dev, HID_EP_IN, &hid_report.mouse, sizeof(hid_report.mouse));
        else if(report_id == ReportIdConsumer)
            usbd_ep_write(usb_dev, HID_EP_IN, &hid_report.consumer, sizeof(hid_report.consumer));
    }
    return true;
}

static void hid_txrx_ep_callback(usbd_device* dev, uint8_t event, uint8_t ep) {
    UNUSED(dev);
    if(event == usbd_evt_eptx) {
        furi_semaphore_release(hid_semaphore);
    } else if(boot_protocol == true) {
        usbd_ep_read(usb_dev, ep, &led_state, sizeof(led_state));
    } else {
        struct HidReportLED leds;
        usbd_ep_read(usb_dev, ep, &leds, sizeof(leds));
        led_state = leds.led_state;
    }
}

/* Configure endpoints */
static usbd_respond hid_ep_config(usbd_device* dev, uint8_t cfg) {
    switch(cfg) {
    case 0:
        /* deconfiguring device */
        usbd_ep_deconfig(dev, HID_EP_OUT);
        usbd_ep_deconfig(dev, HID_EP_IN);
        usbd_reg_endpoint(dev, HID_EP_OUT, 0);
        usbd_reg_endpoint(dev, HID_EP_IN, 0);
        return usbd_ack;
    case 1:
        /* configuring device */
        usbd_ep_config(dev, HID_EP_IN, USB_EPTYPE_INTERRUPT, HID_EP_SZ);
        usbd_ep_config(dev, HID_EP_OUT, USB_EPTYPE_INTERRUPT, HID_EP_SZ);
        usbd_reg_endpoint(dev, HID_EP_IN, hid_txrx_ep_callback);
        usbd_reg_endpoint(dev, HID_EP_OUT, hid_txrx_ep_callback);
        usbd_ep_write(dev, HID_EP_IN, 0, 0);
        boot_protocol = false; /* BIOS will SET_PROTOCOL if it wants this */
        return usbd_ack;
    default:
        return usbd_fail;
    }
}

/* Control requests handler */
static usbd_respond hid_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback) {
    UNUSED(callback);
    /* HID control requests */
    if(((USB_REQ_RECIPIENT | USB_REQ_TYPE) & req->bmRequestType) ==
           (USB_REQ_INTERFACE | USB_REQ_CLASS) &&
       req->wIndex == 1) {
        switch(req->bRequest) {
        case USB_HID_SETIDLE:
            return usbd_ack;
        case USB_HID_GETREPORT:
            if(boot_protocol == true) {
                dev->status.data_ptr = &hid_report.keyboard.boot;
                dev->status.data_count = sizeof(hid_report.keyboard.boot);
            } else {
                dev->status.data_ptr = &hid_report;
                dev->status.data_count = sizeof(hid_report);
            }
            return usbd_ack;
        case USB_HID_SETPROTOCOL:
            if(req->wValue == 0)
                boot_protocol = true;
            else if(req->wValue == 1)
                boot_protocol = false;
            else
                return usbd_fail;
            return usbd_ack;
        default:
            return usbd_fail;
        }
    }
    if(((USB_REQ_RECIPIENT | USB_REQ_TYPE) & req->bmRequestType) ==
           (USB_REQ_INTERFACE | USB_REQ_STANDARD) &&
       req->wIndex == 1 && req->bRequest == USB_STD_GET_DESCRIPTOR) {
        switch(req->wValue >> 8) {
        case USB_DTYPE_HID:
            dev->status.data_ptr = (uint8_t*)&(duo_hid_descriptor);
            dev->status.data_count = sizeof(duo_hid_descriptor);
            return usbd_ack;
        case USB_DTYPE_HID_REPORT:
            boot_protocol = false; /* BIOS does not read this */
            dev->status.data_ptr = (uint8_t*)hid_report_desc;
            dev->status.data_count = sizeof(hid_report_desc);
            return usbd_ack;
        default:
            return usbd_fail;
        }
    }
    return usbd_fail;
}
