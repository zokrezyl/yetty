#ifndef NOMINMAX
#define NOMINMAX
#endif

/*
 * ythorvg.cpp — C API wrapper around YDrawRenderMethod.
 *
 * Owns the ThorVG engine lifetime (ref-counted) and drives a tvg::Picture
 * (wrapped in tvg::Animation for Lottie support) through our custom
 * RenderMethod, which emits ydraw SDF primitives into the supplied
 * yetty_ydraw_drawable_list.
 */

#include <yetty/ythorvg/ythorvg.h>

#include "ydraw-render-method.hpp"

#include <yetty/ytrace/ytrace.h>

// ThorVG public + internal headers
#include <thorvg.h>
#include "tvgPaint.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>

namespace {

// Designated-initializer compound literals from result.h are C-only; build
// result values the long way in C++.
yetty_ythorvg_renderer_ptr_result rp_ok(yetty_ythorvg_renderer* ptr) {
    yetty_ythorvg_renderer_ptr_result r{};
    r.ok = 1;
    r.value = ptr;
    return r;
}

yetty_ythorvg_renderer_ptr_result rp_err(const char* msg) {
    yetty_ythorvg_renderer_ptr_result r{};
    r.ok = 0;
    r.error.msg = msg;
    return r;
}

yetty_ycore_void_result v_ok() {
    yetty_ycore_void_result r{};
    r.ok = 1;
    r.value = 0;
    return r;
}

// Engine init/term is ref-counted across all renderers.
std::mutex g_engine_mutex;
int g_engine_refcount = 0;

bool engine_acquire() {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine_refcount == 0) {
        auto r = tvg::Initializer::init(0);
        if (r != tvg::Result::Success) {
            yerror("ythorvg: tvg::Initializer::init failed");
            return false;
        }
        uint32_t major = 0, minor = 0, micro = 0;
        const char* version = tvg::Initializer::version(&major, &minor, &micro);
        ydebug("ythorvg: engine initialized %s", version ? version : "unknown");
    }
    ++g_engine_refcount;
    return true;
}

void engine_release() {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine_refcount <= 0) { return;
}
    if (--g_engine_refcount == 0) {
        tvg::Initializer::term();
        ydebug("ythorvg: engine terminated");
    }
}

} // namespace

struct yetty_ythorvg_renderer {
    yetty::ythorvg::YDrawRenderMethod* render_method = nullptr;  // ref-counted by ThorVG
    std::unique_ptr<tvg::Animation> animation;
    tvg::Picture* picture = nullptr;  // borrowed from animation
    float total_frames = 0.0f;
    float duration = 0.0f;
    bool is_animation = false;
    bool engine_held = false;
};

namespace {

void render_once(yetty_ythorvg_renderer* r) {
    if (!r || !r->picture || !r->render_method) { return;
}

    auto* paintImpl = PAINT(r->picture);
    if (!paintImpl) { return;
}

    r->render_method->preRender();

    tvg::Array<tvg::RenderData> clips;
    auto identity = tvg::Matrix{1, 0, 0, 0, 1, 0, 0, 0, 1};
    paintImpl->update(r->render_method, identity, clips, 255, tvg::RenderUpdateFlag::All);
    paintImpl->render(r->render_method);
    r->render_method->postRender();
}

const char* detect_mimetype(const void* data, size_t size) {
    if (!data || size == 0) { return nullptr;
}
    std::string_view sv(static_cast<const char*>(data), size);
    if (sv.find("<svg") != std::string_view::npos ||
        sv.find("<?xml") != std::string_view::npos) {
        return "svg";
    }
    if (sv.find("\"v\"") != std::string_view::npos &&
        sv.find("\"layers\"") != std::string_view::npos) {
        return "lottie";
    }
    return nullptr;
}

} // namespace

extern "C" {

struct yetty_ythorvg_renderer_ptr_result
yetty_ythorvg_renderer_create(struct yetty_ydraw_drawable_list* buf) {
    if (!buf) {
        return rp_err("yetty_ythorvg_renderer_create: buffer is NULL");
    }
    if (!engine_acquire()) {
        return rp_err("yetty_ythorvg_renderer_create: engine init failed");
    }

    auto* r = new yetty_ythorvg_renderer();
    r->engine_held = true;
    r->render_method = new yetty::ythorvg::YDrawRenderMethod(buf);
    r->render_method->ref();  // ThorVG manages lifetime via ref/unref.

    return rp_ok(r);
}

void yetty_ythorvg_renderer_destroy(struct yetty_ythorvg_renderer* r) {
    if (!r) { return;
}

    // Animation must be destroyed before the render method — it uses the
    // render method to dispose its own render data.
    r->animation.reset();
    r->picture = nullptr;

    if (r->render_method) {
        r->render_method->unref();
        r->render_method = nullptr;
    }

    if (r->engine_held) {
        engine_release();
        r->engine_held = false;
    }

    delete r;
}

void yetty_ythorvg_renderer_set_target(struct yetty_ythorvg_renderer* r,
                                       uint32_t width, uint32_t height) {
    if (!r || !r->render_method) { return;
}
    r->render_method->setTarget(width, height);
}

struct yetty_ycore_void_result
yetty_ythorvg_renderer_render(struct yetty_ythorvg_renderer* r,
                     const void* data, size_t size,
                     const char* mimetype,
                     float* out_width, float* out_height) {
    if (!r || !r->render_method) {
        return yetty_ycore_err("yetty_ythorvg_render: renderer is NULL");
    }
    if (!data || size == 0) {
        return yetty_ycore_err("yetty_ythorvg_render: data is empty");
    }

    if (!mimetype) { mimetype = detect_mimetype(data, size);
}

    // Fresh Animation wrapper — replaces any previously loaded content.
    r->animation.reset(tvg::Animation::gen());
    if (!r->animation) {
        return yetty_ycore_err("yetty_ythorvg_render: tvg::Animation::gen failed");
    }

    r->picture = r->animation->picture();
    if (!r->picture) {
        r->animation.reset();
        return yetty_ycore_err("yetty_ythorvg_render: Animation::picture() returned NULL");
    }

    tvg::Result load_result;
    bool is_lottie = (mimetype && std::strcmp(mimetype, "lottie") == 0);
    load_result = r->picture->load(static_cast<const char*>(data),
                                   static_cast<uint32_t>(size),
                                   mimetype ? mimetype : "", nullptr, true);
    if (load_result != tvg::Result::Success) {
        r->animation.reset();
        r->picture = nullptr;
        return yetty_ycore_err("yetty_ythorvg_render: picture->load failed");
    }

    if (is_lottie) {
        r->total_frames = r->animation->totalFrame();
        r->duration = r->animation->duration();
        r->is_animation = r->total_frames > 1.0f;
    } else {
        r->total_frames = 0.0f;
        r->duration = 0.0f;
        r->is_animation = false;
    }

    float w = 0.0f, h = 0.0f;
    r->picture->size(&w, &h);
    if (out_width) { *out_width = w;
}
    if (out_height) { *out_height = h;
}

    render_once(r);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result
yetty_ythorvg_renderer_render_frame(struct yetty_ythorvg_renderer* r, float frame) {
    if (!r || !r->render_method) {
        return yetty_ycore_err("yetty_ythorvg_render_frame: renderer is NULL");
    }
    if (!r->is_animation || !r->animation) {
        return yetty_ycore_err("yetty_ythorvg_render_frame: no animation loaded");
    }
    r->animation->frame(frame);
    render_once(r);
    return YETTY_OK_VOID();
}

float yetty_ythorvg_renderer_total_frames(const struct yetty_ythorvg_renderer* r) {
    return r ? r->total_frames : 0.0f;
}

float yetty_ythorvg_renderer_duration(const struct yetty_ythorvg_renderer* r) {
    return r ? r->duration : 0.0f;
}

} // extern "C"
