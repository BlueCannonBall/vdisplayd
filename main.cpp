#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <evdi_lib.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <vector>

// clang-format off
unsigned char dummy_edid[] = {
  0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x06, 0xaf, 0xb0, 0xe6,
  0x00, 0x00, 0x00, 0x00, 0x2c, 0x21, 0x01, 0x04, 0x95, 0x22, 0x13, 0x78,
  0x02, 0x05, 0xb5, 0x94, 0x59, 0x59, 0x92, 0x28, 0x1d, 0x50, 0x54, 0x00,
  0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x78, 0x37, 0x80, 0xb4, 0x70, 0x38,
  0x2e, 0x40, 0x6c, 0x30, 0xaa, 0x00, 0x58, 0xc1, 0x10, 0x00, 0x00, 0x18,
  0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x41,
  0x55, 0x4f, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x00, 0x00, 0x00, 0xfe, 0x00, 0x42, 0x31, 0x35, 0x36, 0x48, 0x41, 0x4b,
  0x30, 0x32, 0x2e, 0x35, 0x20, 0x0a, 0x00, 0x3f
};
// clang-format on

struct DaemonContext {
    evdi_handle handle;
    std::vector<uint8_t> buf;
    int buffer_id = 1;
    bool is_registered = false;
};

// Grab pixels and immediately request the next update.
// This is what signals the DRM flip_done fence in the kernel.
// Must be called whenever pixels are available — either from
// the update_ready event OR when evdi_request_update returns true.
static void do_grab_and_request(DaemonContext* ctx) {
    struct evdi_rect rects[16];
    int num_rects = 16;

    evdi_grab_pixels(ctx->handle, rects, &num_rects);

    // Request next update. If dirts are already available (returns true),
    // grab again immediately to avoid stalling the DRM flip pipeline.
    while (evdi_request_update(ctx->handle, ctx->buffer_id)) {
        num_rects = 16;
        evdi_grab_pixels(ctx->handle, rects, &num_rects);
    }
}

static void dpms_handler(int dpms_mode, void* user_data) {
    std::cout << "DPMS state: " << dpms_mode << std::endl;
}

static void mode_changed_handler(struct evdi_mode mode, void* user_data) {
    DaemonContext* ctx = (DaemonContext*) user_data;
    std::cout << "Mode: " << mode.width << "x" << mode.height << " @ " << mode.refresh_rate << "Hz" << std::endl;

    // Allocate memory for the new mode
    size_t bytes_per_pixel = mode.bits_per_pixel / 8;
    size_t stride = mode.width * bytes_per_pixel;
    ctx->buf.resize(stride * mode.height);

    // Register buffer with the new dimensions
    struct evdi_buffer buffer = {
        .id = ctx->buffer_id,
        .buffer = ctx->buf.data(),
        .width = mode.width,
        .height = mode.height,
        .stride = (int) stride,
    };

    if (ctx->is_registered) {
        evdi_unregister_buffer(ctx->handle, ctx->buffer_id);
    }

    evdi_register_buffer(ctx->handle, buffer);
    ctx->is_registered = true;

    // Request the first update. If the kernel already has dirty pixels
    // (which it always does on first modeset), grab them immediately.
    // Ignoring this return value is what causes the 10s flip_done timeout.
    if (evdi_request_update(ctx->handle, ctx->buffer_id)) {
        do_grab_and_request(ctx);
    }
}

static void update_ready_handler(int buffer_to_be_updated, void* user_data) {
    DaemonContext* ctx = (DaemonContext*) user_data;
    do_grab_and_request(ctx);
}

int main() {
    DaemonContext ctx;

    if ((ctx.handle = evdi_open_attached_to_fixed(nullptr, 0)) == EVDI_INVALID_HANDLE) {
        std::cerr << "Error: Could not open EVDI device" << std::endl;
        return 1;
    }

    evdi_enable_cursor_events(ctx.handle, true);

    evdi_connect(ctx.handle, dummy_edid, sizeof(dummy_edid), 3840 * 2160);

    // Link the evdi provider to the primary GPU so X11 can render to it.
    if (Display* dpy = XOpenDisplay(nullptr)) {
        Window root = DefaultRootWindow(dpy);
        XRRScreenResources* res = XRRGetScreenResources(dpy, root);
        XRRProviderResources* prov_res = XRRGetProviderResources(dpy, root);

        RRProvider source = 0;
        RRProvider sink = 0;
        for (int i = 0; i < prov_res->nproviders; ++i) {
            XRRProviderInfo* info = XRRGetProviderInfo(dpy, res, prov_res->providers[i]);
            if (std::string(info->name).find("evdi") != std::string::npos) {
                sink = prov_res->providers[i];
            } else if (!source) {
                source = prov_res->providers[i];
            }
            XRRFreeProviderInfo(info);

            if (sink && source) {
                XRRSetProviderOutputSource(dpy, sink, source);
                break;
            }
        }

        XRRFreeProviderResources(prov_res);
        XRRFreeScreenResources(res);
        XCloseDisplay(dpy);
    } else {
        std::cerr << "Warning: Could not open X11 display, continuing anyway..." << std::endl;
    }

    struct evdi_event_context ev_ctx = {0};
    ev_ctx.user_data = &ctx;
    ev_ctx.dpms_handler = dpms_handler;
    ev_ctx.mode_changed_handler = mode_changed_handler;
    ev_ctx.update_ready_handler = update_ready_handler;

    evdi_selectable fd = evdi_get_event_ready(ctx.handle);

    std::cout << "Daemon active." << std::endl;

    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    for (;;) {
        if (poll(&pfd, 1, 16) > 0) {
            evdi_handle_events(ctx.handle, &ev_ctx);
        }
    }

    evdi_close(ctx.handle);
    return 0;
}
