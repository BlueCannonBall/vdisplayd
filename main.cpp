#include <evdi_lib.h>
#include <iostream>
#include <poll.h>
#include <vector>

#define BUF_ID 1

// clang-format off
unsigned char edid[] = {
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
    std::vector<uint8_t> buf_data;
    bool buf_registered = false;
};

static void mode_changed_handler(evdi_mode mode, void* data) {
    std::cout << "Mode: " << mode.width << "x" << mode.height << " @ " << mode.refresh_rate << "Hz" << std::endl;

    auto* ctx = (DaemonContext*) data;

    if (ctx->buf_registered) {
        evdi_unregister_buffer(ctx->handle, BUF_ID);
        ctx->buf_registered = false;
    }

    int stride = mode.width * (mode.bits_per_pixel / 8);
    ctx->buf_data.resize(stride * mode.height);

    struct evdi_buffer buf = {
        .id = BUF_ID,
        .buffer = ctx->buf_data.data(),
        .width = mode.width,
        .height = mode.height,
        .stride = stride,
    };
    evdi_register_buffer(ctx->handle, buf);
    ctx->buf_registered = true;

    while (evdi_request_update(ctx->handle, BUF_ID)) {
        evdi_rect rects[16];
        int rect_count = 16;
        evdi_grab_pixels(ctx->handle, rects, &rect_count);
    }
}

int main() {
    DaemonContext ctx;
    if ((ctx.handle = evdi_open_attached_to_fixed(nullptr, 0)) == EVDI_INVALID_HANDLE) {
        std::cerr << "Error: Could not open EVDI device" << std::endl;
        return 1;
    }
    evdi_enable_cursor_events(ctx.handle, true);
    evdi_connect(ctx.handle, edid, sizeof(edid), 3840 * 2160);

    struct evdi_event_context event_ctx = {
        .mode_changed_handler = mode_changed_handler,
        .user_data = &ctx,
    };
    pollfd pfd = {.fd = evdi_get_event_ready(ctx.handle), .events = POLLIN};
    for (;;) {
        if (poll(&pfd, 1, -1) > 0) {
            evdi_handle_events(ctx.handle, &event_ctx);
        }
    }
}
