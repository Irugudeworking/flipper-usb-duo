#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include "helpers/ducky_script.h"
#include "helpers/mass_storage_usb.h"

_Static_assert(sizeof(DialogsFileBrowserOptions) >= 32, "Build with Momentum SDK, not stock SDK");

typedef struct {
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    File* image;
    uint32_t blocks;
    uint32_t read_bytes;
    uint32_t written_bytes;
    bool ejected;
    BadUsbState state;
    bool active;
    uint8_t selected;
    char image_name[48];
    char script_name[48];
    char layout_name[48];
    uint8_t frame[1024];
    bool capture_frame;
    bool frame_ready;
} Duo;

static bool disk_read(void* ctx, uint32_t lba, uint16_t count, uint8_t* out, uint32_t* len, uint32_t cap) {
    Duo* d = ctx;
    if(lba >= d->blocks || count > d->blocks - lba) return false;
    uint32_t size = MIN(cap, (uint32_t)count * SCSI_BLOCK_SIZE);
    if(!storage_file_seek(d->image, lba * SCSI_BLOCK_SIZE, true)) return false;
    *len = storage_file_read(d->image, out, size);
    furi_mutex_acquire(d->mutex, FuriWaitForever);
    d->read_bytes += *len;
    furi_mutex_release(d->mutex);
    return *len == size;
}
static bool disk_write(void* ctx, uint32_t lba, uint16_t count, uint8_t* data, uint32_t len) {
    Duo* d = ctx;
    if(lba >= d->blocks || count > d->blocks - lba || len != (uint32_t)count * SCSI_BLOCK_SIZE) return false;
    if(!storage_file_seek(d->image, lba * SCSI_BLOCK_SIZE, true)) return false;
    bool ok = storage_file_write(d->image, data, len) == len;
    // Finish writes before reporting success to the host.
    ok = storage_file_sync(d->image) && ok;
    furi_mutex_acquire(d->mutex, FuriWaitForever);
    d->written_bytes += ok ? len : 0;
    furi_mutex_release(d->mutex);
    return ok;
}
static uint32_t disk_size(void* ctx) { return ((Duo*)ctx)->blocks; }
static void disk_eject(void* ctx) {
    Duo* d = ctx;
    storage_file_sync(d->image);
    furi_mutex_acquire(d->mutex, FuriWaitForever);
    d->ejected = true;
    furi_mutex_release(d->mutex);
}
static void input_cb(InputEvent* e, void* ctx) {
    Duo* d = ctx;
    furi_message_queue_put(d->queue, e, 0);
}
static const char* state_name(BadUsbWorkerState s) {
    static const char* names[] = {"Loading", "Disconnected", "Ready", "Waiting for USB", "Running", "Delay", "Typing", "Press OK", "Paused", "Complete", "Script error", "File error"};
    return s <= BadUsbStateFileError ? names[s] : "Unknown";
}
static void title(Canvas* c, const char* right) {
    canvas_set_color(c, ColorBlack);
    canvas_draw_box(c, 0, 0, 128, 13);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 4, 10, "USB DUO");
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 124, 10, AlignRight, AlignBottom, right);
    canvas_set_color(c, ColorBlack);
}
static void row(Canvas* c, int y, const char* label, const char* value, bool selected) {
    if(selected) {
        canvas_draw_rbox(c, 1, y-8, 126, 11, 2);
        canvas_set_color(c, ColorWhite);
    }
    canvas_draw_str(c, 4, y, label);
    char short_value[24];
    snprintf(short_value, sizeof(short_value), "%s", value);
    bool clipped = false;
    while(canvas_string_width(c, short_value) > 74 && strlen(short_value) > 4) {
        short_value[strlen(short_value)-1] = '\0';
        clipped = true;
    }
    if(clipped && strlen(short_value) >= 3) {
        size_t n = strlen(short_value);
        short_value[n-3] = '.'; short_value[n-2] = '.'; short_value[n-1] = '.';
    }
    canvas_draw_str_aligned(c, 123, y, AlignRight, AlignBottom, short_value);
    canvas_set_color(c, ColorBlack);
}
static void draw_cb(Canvas* c, void* ctx) {
    Duo* d = ctx;
    furi_mutex_acquire(d->mutex, FuriWaitForever);
    canvas_clear(c);
    canvas_set_font(c, FontSecondary);
    if(!d->active) {
        title(c, "SETUP");
        row(c, 22, "Disk", d->image_name, d->selected==0);
        row(c, 33, "Script", d->script_name, d->selected==1);
        row(c, 44, "Layout", d->layout_name, d->selected==2);
        row(c, 55, "Connect USB", "Start >", d->selected==3);
        canvas_draw_str(c, 3, 64, d->selected==2 ? "OK: browse   Left: US   Back: exit" : "Up/Down: select   OK: open");
    } else {
        title(c, "HID + DISK");
        char line[64];
        snprintf(line, sizeof(line), "%.27s", d->script_name);
        canvas_draw_str(c, 3, 23, line);
        snprintf(line, sizeof(line), "%s %zu/%zu", state_name(d->state.state), d->state.line_cur, d->state.line_nb);
        canvas_draw_str(c, 3, 34, line);
        float progress = d->state.line_nb ? (float)d->state.line_cur / d->state.line_nb : 0;
        elements_progress_bar(c, 3, 37, 122, MIN(progress, 1.0f));
        if(d->state.state==BadUsbStateScriptError) {
            snprintf(line, sizeof(line), "L%zu: %.26s", d->state.error_line, d->state.error);
        } else if(d->ejected) {
            snprintf(line, sizeof(line), "Drive ejected - safe to leave");
        } else {
            snprintf(line, sizeof(line), "R %luK  W %luK | %.12s", d->read_bytes/1024, d->written_bytes/1024, d->image_name);
        }
        canvas_draw_str(c, 3, 53, line);
        bool idle = d->state.state==BadUsbStateIdle || d->state.state==BadUsbStateDone || d->state.state==BadUsbStateNotConnected;
        elements_button_left(c, "Setup");
        elements_button_center(c, idle ? "Run" : "Stop");
        elements_button_right(c, d->state.state==BadUsbStatePaused ? "Resume" : "Pause");
    }

    furi_mutex_release(d->mutex);
}
static void frame_cb(uint8_t* data, size_t size, CanvasOrientation orientation, void* ctx) {
    UNUSED(orientation);
    Duo* d=ctx;
    if(furi_mutex_acquire(d->mutex,0)!=FuriStatusOk) return;
    if(d->capture_frame && size==sizeof(d->frame)) {
        memcpy(d->frame,data,size);d->frame_ready=true;d->capture_frame=false;
    }
    furi_mutex_release(d->mutex);
}
static bool choose(DialogsApp* dialogs, ViewPort* view, FuriString* path, const char* ext, const char* base) {
    // Momentum's options structure is larger than the stock SDK structure.
    // This app must be built with the Momentum SDK; see application.fam.
    DialogsFileBrowserOptions opts = {0};
    dialog_file_browser_set_basic_options(&opts, ext, NULL);
    opts.base_path = base;
    opts.skip_assets = false;
    FuriString* start = furi_string_empty(path) ? furi_string_alloc_set(base) : furi_string_alloc_set(path);
    FuriString* selected = furi_string_alloc_set(start);
    view_port_enabled_set(view, false);
    bool ok = dialog_file_browser_show(dialogs, selected, start, &opts);
    if(ok) furi_string_set(path, selected);
    furi_string_free(selected);
    furi_string_free(start);
    view_port_enabled_set(view, true);
    return ok;
}
static DialogMessageButton message(DialogsApp* dialogs, ViewPort* view, const char* heading, const char* text, bool confirm) {
    view_port_enabled_set(view, false);
    DialogMessage* m = dialog_message_alloc();
    dialog_message_set_header(m, heading, 64, 3, AlignCenter, AlignTop);
    dialog_message_set_text(m, text, 64, 30, AlignCenter, AlignCenter);
    dialog_message_set_buttons(m, confirm ? "Stay" : NULL, confirm ? NULL : "OK", confirm ? "Disconnect" : NULL);
    DialogMessageButton button = dialog_message_show(dialogs, m);
    dialog_message_free(m);
    view_port_enabled_set(view, true);
    return button;
}
static void refresh_names(Duo* d, FuriString* image, FuriString* script, FuriString* layout) {
    FuriString* paths[] = {image, script, layout};
    char* names[] = {d->image_name, d->script_name, d->layout_name};
    furi_mutex_acquire(d->mutex, FuriWaitForever);
    for(size_t i=0;i<3;i++) {
        const char* text=furi_string_get_cstr(paths[i]);
        const char* slash=strrchr(text, '/');
        snprintf(names[i], 48, "%s", *text ? (slash ? slash+1 : text) : (i==2 ? "US default" : "Select..."));
    }
    furi_mutex_release(d->mutex);
}
#define DUO_CONFIG "/ext/apps_data/usb_duo/settings.ff"
static void settings(Storage* storage, FuriString* image, FuriString* script, FuriString* layout, bool write) {
    FlipperFormat* f = flipper_format_file_alloc(storage);
    if(write) {
        if(flipper_format_file_open_always(f, DUO_CONFIG)) {
            flipper_format_write_header_cstr(f, "USB Duo", 1);
            flipper_format_write_string(f, "Image", image);
            flipper_format_write_string(f, "Script", script);
            flipper_format_write_string(f, "Layout", layout);
        }
    } else if(flipper_format_file_open_existing(f, DUO_CONFIG)) {
        flipper_format_read_string(f, "Image", image);
        flipper_format_read_string(f, "Script", script);
        flipper_format_read_string(f, "Layout", layout);
    }
    flipper_format_free(f);
}
int32_t usb_duo_app(void* p) {
    bool selftest = p && strcmp((const char*)p, "selftest") == 0;
    bool uitest = p && strcmp((const char*)p, "uitest") == 0;
    bool diagnostic = selftest || uitest;
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Gui* gui = furi_record_open(RECORD_GUI);
    FuriString* image_path = furi_string_alloc_set("/ext/apps_data/usb_duo/DUO.img");
    FuriString* script_path = furi_string_alloc();
    FuriString* layout = furi_string_alloc();
    storage_simply_mkdir(storage, "/ext/apps_data/usb_duo");
    if(!diagnostic) settings(storage, image_path, script_path, layout, false);
    Duo* d = calloc(1, sizeof(Duo));
    d->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    d->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    d->image = storage_file_alloc(storage);
    ViewPort* view = view_port_alloc();
    view_port_draw_callback_set(view, draw_cb, d);
    view_port_input_callback_set(view, input_cb, d);
    gui_add_view_port(gui, view, GuiLayerFullscreen);
    if(diagnostic) gui_add_framebuffer_callback(gui,frame_cb,d);
    refresh_names(d, image_path, script_path, layout);
    view_port_update(view);
    bool running=true;
    while(running) {
        bool connect=selftest;
        if(selftest) furi_string_set(script_path, "/ext/badusb/usb_duo_selftest.txt");
        if(!connect) {
            InputEvent e;
            if(furi_message_queue_get(d->queue, &e, 500)!=FuriStatusOk) { view_port_update(view); continue; }
            if(e.type!=InputTypeShort && e.type!=InputTypeRepeat) continue;
            if(e.key==InputKeyBack) break;
            if(e.key==InputKeyUp || e.key==InputKeyDown) {
                furi_mutex_acquire(d->mutex, FuriWaitForever);
                d->selected=(d->selected+(e.key==InputKeyDown ? 1 : 3))%4;
                furi_mutex_release(d->mutex);
            } else if(e.key==InputKeyLeft && d->selected==2) {
                furi_string_reset(layout);
            } else if(e.key==InputKeyOk) {
                if(d->selected==0) {
                    choose(dialogs, view, image_path, ".img", "/ext");
                } else if(d->selected==1) {
                    choose(dialogs, view, script_path, ".txt", "/ext/badusb");
                } else if(d->selected==2) {
                    FuriString* candidate=furi_string_empty(layout) ? furi_string_alloc_set("/ext/badusb/assets/layouts") : furi_string_alloc_set(layout);
                    if(choose(dialogs, view, candidate, ".kl", "/ext/badusb/assets/layouts")) furi_string_set(layout, candidate);
                    furi_string_free(candidate);
                } else connect=true;
                // A release from the browser must not activate a setup action.
                furi_message_queue_reset(d->queue);
            }
            refresh_names(d, image_path, script_path, layout);
            view_port_update(view);
        }
        if(!connect) continue;
        if(furi_hal_usb_is_locked()) {
            message(dialogs, view, "USB busy", "Close qFlipper / RPC\nthen connect again.", false);
            continue;
        }
        if(furi_string_empty(script_path) || !storage_file_exists(storage, furi_string_get_cstr(script_path))) {
            message(dialogs, view, "Choose a script", "Select a DuckyScript .txt\nfrom the Script row.", false);
            continue;
        }
        if(!storage_file_open(d->image, furi_string_get_cstr(image_path), FSAM_READ|FSAM_WRITE, FSOM_OPEN_EXISTING)) {
            message(dialogs, view, "Choose a disk", "Select an existing .img\nfrom the Disk row.", false);
            continue;
        }
        uint64_t bytes=storage_file_size(d->image);
        if(bytes<512 || bytes%512 || bytes>UINT32_MAX) {
            storage_file_close(d->image);
            message(dialogs, view, "Invalid image", "Image must use 512-byte\nsectors and be under 4 GiB.", false);
            continue;
        }
        d->blocks=bytes/512;
        if(!diagnostic) settings(storage, image_path, script_path, layout, true);
        refresh_names(d, image_path, script_path, layout);
        furi_mutex_acquire(d->mutex, FuriWaitForever);
        d->active=true;d->ejected=false;d->read_bytes=0;d->written_bytes=0;
        memset(&d->state, 0, sizeof(d->state));
        furi_mutex_release(d->mutex);
        SCSIDeviceFunc disk={.ctx=d,.read=disk_read,.write=disk_write,.num_blocks=disk_size,.eject=disk_eject};
        const char* device_name=furi_hal_version_get_device_name_ptr();
        if(!device_name) device_name="Flipper";
        char usb_serial[48];
        snprintf(usb_serial,sizeof(usb_serial),"USB-Duo-%s",device_name);
        MassStorageUsb* usb=mass_storage_usb_start(usb_serial,disk);
        if(!usb) {
            storage_file_close(d->image);
            furi_mutex_acquire(d->mutex,FuriWaitForever);d->active=false;furi_mutex_release(d->mutex);
            message(dialogs,view,"USB busy","Try connecting again.",false);continue;
        }
        BadUsbScript* script=bad_usb_script_open(script_path,BadUsbHidInterfaceUsb);
        bad_usb_script_set_keyboard_layout(script,layout);
        bool session=true, test_started=false, frame_saved=false;
        uint32_t start_tick=furi_get_tick();
        while(session) {
            uint32_t elapsed=furi_get_tick()-start_tick;
            if(diagnostic) {
                if(elapsed>furi_ms_to_ticks(15000) && !test_started) {
                    bad_usb_script_start_stop(script);test_started=true;
                }
                if(elapsed>furi_ms_to_ticks(60000)) session=false;
                if(elapsed>furi_ms_to_ticks(5000) && !frame_saved) {
                    furi_mutex_acquire(d->mutex,FuriWaitForever);
                    if(!d->frame_ready) d->capture_frame=true;
                    else {
                        File* screen=storage_file_alloc(storage);
                        if(storage_file_open(screen,"/ext/apps_data/usb_duo/active.bin",FSAM_WRITE,FSOM_CREATE_ALWAYS)) storage_file_write(screen,d->frame,sizeof(d->frame));
                        storage_file_free(screen);frame_saved=true;
                    }
                    furi_mutex_release(d->mutex);
                }
            }
            InputEvent e;
            if(furi_message_queue_get(d->queue,&e,100)==FuriStatusOk && e.type==InputTypeShort) {
                if(e.key==InputKeyBack || e.key==InputKeyLeft) {
                    if(d->ejected || message(dialogs,view,"Disconnect USB?","Eject the disk on your PC\nbefore disconnecting.",true)==DialogMessageButtonRight) session=false;
                } else if(e.key==InputKeyOk) bad_usb_script_start_stop(script);
                else if(e.key==InputKeyRight) bad_usb_script_pause_resume(script);
            }
            furi_mutex_acquire(d->mutex,FuriWaitForever);
            d->state=*bad_usb_script_get_state(script);
            furi_mutex_release(d->mutex);
            view_port_update(view);
        }
        bad_usb_script_close(script);
        mass_storage_usb_stop(usb);
        storage_file_sync(d->image);
        storage_file_close(d->image);
        furi_mutex_acquire(d->mutex,FuriWaitForever);d->active=false;furi_mutex_release(d->mutex);
        furi_message_queue_reset(d->queue);
        view_port_update(view);
        if(selftest) running=false;
    }
    if(diagnostic) gui_remove_framebuffer_callback(gui,frame_cb,d);
    view_port_enabled_set(view,false);
    gui_remove_view_port(gui,view);
    view_port_free(view);
    storage_file_free(d->image);
    furi_message_queue_free(d->queue);
    furi_mutex_free(d->mutex);
    free(d);
    furi_string_free(layout);furi_string_free(script_path);furi_string_free(image_path);
    furi_record_close(RECORD_GUI);furi_record_close(RECORD_STORAGE);furi_record_close(RECORD_DIALOGS);
    return 0;
}
