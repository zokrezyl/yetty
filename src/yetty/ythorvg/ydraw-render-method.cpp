#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ydraw-render-method.hpp"

#include <yetty/ypaint-core/buffer.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ytrace.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

//=============================================================================
// Bezier curve flattening (adaptive subdivision)
//=============================================================================

// Compute squared distance from point to line segment
static float pointToSegmentDistSq(float px, float py,
                                   float x0, float y0, float x1, float y1) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float lenSq = dx * dx + dy * dy;

    if (lenSq < 1e-10f) {
        float ddx = px - x0;
        float ddy = py - y0;
        return ddx * ddx + ddy * ddy;
    }

    float t = ((px - x0) * dx + (py - y0) * dy) / lenSq;
    t = std::clamp(t, 0.0f, 1.0f);

    float projX = x0 + t * dx;
    float projY = y0 + t * dy;
    float ddx = px - projX;
    float ddy = py - projY;
    return ddx * ddx + ddy * ddy;
}

// Flatten cubic bezier into line segments via adaptive subdivision.
// Appends (x,y) pairs to `out`. Start point p0 is NOT added (caller adds it).
static void flattenCubicBezier(float p0x, float p0y,
                                float p1x, float p1y,
                                float p2x, float p2y,
                                float p3x, float p3y,
                                float toleranceSq,
                                std::vector<float>& out,
                                int depth = 0) {
    constexpr int MAX_DEPTH = 12;

    if (depth >= MAX_DEPTH) {
        out.push_back(p3x);
        out.push_back(p3y);
        return;
    }

    float d1Sq = pointToSegmentDistSq(p1x, p1y, p0x, p0y, p3x, p3y);
    float d2Sq = pointToSegmentDistSq(p2x, p2y, p0x, p0y, p3x, p3y);

    if (d1Sq <= toleranceSq && d2Sq <= toleranceSq) {
        out.push_back(p3x);
        out.push_back(p3y);
        return;
    }

    float p01x = (p0x + p1x) * 0.5f, p01y = (p0y + p1y) * 0.5f;
    float p12x = (p1x + p2x) * 0.5f, p12y = (p1y + p2y) * 0.5f;
    float p23x = (p2x + p3x) * 0.5f, p23y = (p2y + p3y) * 0.5f;

    float p012x = (p01x + p12x) * 0.5f, p012y = (p01y + p12y) * 0.5f;
    float p123x = (p12x + p23x) * 0.5f, p123y = (p12y + p23y) * 0.5f;

    float midx = (p012x + p123x) * 0.5f, midy = (p012y + p123y) * 0.5f;

    flattenCubicBezier(p0x, p0y, p01x, p01y, p012x, p012y, midx, midy,
                       toleranceSq, out, depth + 1);
    flattenCubicBezier(midx, midy, p123x, p123y, p23x, p23y, p3x, p3y,
                       toleranceSq, out, depth + 1);
}

namespace yetty::ythorvg {

//=============================================================================
// Constructor/Destructor
//=============================================================================

YDrawRenderMethod::YDrawRenderMethod(yetty_ypaint_core_buffer* buffer)
    : _buffer(buffer) {}

YDrawRenderMethod::~YDrawRenderMethod() {
    for (auto* rd : _renderDataList) {
        delete rd;
    }
    _renderDataList.clear();
}

void YDrawRenderMethod::setTarget(uint32_t width, uint32_t height) {
    _width = width;
    _height = height;
}

//=============================================================================
// Lifecycle methods
//=============================================================================

bool YDrawRenderMethod::preUpdate() {
    return true;
}

bool YDrawRenderMethod::postUpdate() {
    return true;
}

bool YDrawRenderMethod::preRender() {
    if (_buffer) { yetty_ypaint_core_buffer_clear(_buffer);
}
    _nextPrimId = 0;
    return true;
}

bool YDrawRenderMethod::postRender() {
    return true;
}

bool YDrawRenderMethod::clear() {
    if (_buffer) { yetty_ypaint_core_buffer_clear(_buffer);
}
    _nextPrimId = 0;
    return true;
}

bool YDrawRenderMethod::sync() {
    return true;
}

//=============================================================================
// Shape preparation
//=============================================================================

tvg::RenderData YDrawRenderMethod::prepare(
    const tvg::RenderShape& rshape, tvg::RenderData data,
    const tvg::Matrix& transform, tvg::Array<tvg::RenderData>& clips,
    uint8_t opacity, tvg::RenderUpdateFlag flags, bool clipper)
{
    (void)clips;
    (void)flags;
    (void)clipper;

    auto* rd = static_cast<YDrawRenderData*>(data);
    if (!rd) {
        rd = new YDrawRenderData();
        _renderDataList.push_back(rd);
    }

    rd->type = YDrawRenderData::Type::Shape;
    rd->valid = true;

    rd->cmds.clear();
    rd->pts.clear();
    for (uint32_t i = 0; i < rshape.path.cmds.count; ++i) {
        rd->cmds.push_back(rshape.path.cmds[i]);
    }
    for (uint32_t i = 0; i < rshape.path.pts.count; ++i) {
        rd->pts.push_back(rshape.path.pts[i]);
    }

    rd->transform = transform;
    rd->opacity = opacity;

    rshape.fillColor(&rd->fillR, &rd->fillG, &rd->fillB, &rd->fillA);
    rd->fill = rshape.fill;
    rd->rule = rshape.rule;

    if (rshape.stroke) {
        rd->strokeWidth = rshape.stroke->width;
        rd->strokeR = rshape.stroke->color.r;
        rd->strokeG = rshape.stroke->color.g;
        rd->strokeB = rshape.stroke->color.b;
        rd->strokeA = rshape.stroke->color.a;
        rd->strokeFill = rshape.stroke->fill;
        rd->strokeCap = rshape.stroke->cap;
        rd->strokeJoin = rshape.stroke->join;

        rd->dashPattern.clear();
        if (rshape.stroke->dash.pattern && rshape.stroke->dash.count > 0) {
            for (uint32_t i = 0; i < rshape.stroke->dash.count; ++i) {
                rd->dashPattern.push_back(rshape.stroke->dash.pattern[i]);
            }
            rd->dashOffset = rshape.stroke->dash.offset;
        }
    } else {
        rd->strokeWidth = 0.0f;
        rd->strokeR = rd->strokeG = rd->strokeB = rd->strokeA = 0;
        rd->strokeFill = nullptr;
        rd->dashPattern.clear();
        rd->dashOffset = 0.0f;
    }

    if (!rd->pts.empty()) {
        float minX = 1e10f, minY = 1e10f, maxX = -1e10f, maxY = -1e10f;
        for (const auto& pt : rd->pts) {
            auto tp = transformPoint(pt, rd->transform);
            minX = std::min(minX, tp.x);
            minY = std::min(minY, tp.y);
            maxX = std::max(maxX, tp.x);
            maxY = std::max(maxY, tp.y);
        }
        rd->bounds = {{static_cast<int32_t>(minX), static_cast<int32_t>(minY)},
                      {static_cast<int32_t>(maxX + 1), static_cast<int32_t>(maxY + 1)}};
    }

    return rd;
}

tvg::RenderData YDrawRenderMethod::prepare(
    tvg::RenderSurface* surface, tvg::RenderData data,
    const tvg::Matrix& transform, tvg::Array<tvg::RenderData>& clips,
    uint8_t opacity, tvg::RenderUpdateFlag flags)
{
    (void)clips;
    (void)flags;

    auto* rd = static_cast<YDrawRenderData*>(data);
    if (!rd) {
        rd = new YDrawRenderData();
        _renderDataList.push_back(rd);
    }

    rd->type = YDrawRenderData::Type::Image;
    rd->valid = true;
    rd->surface = surface;
    rd->transform = transform;
    rd->opacity = opacity;

    if (surface) {
        rd->bounds = {{0, 0},
                      {static_cast<int32_t>(surface->w), static_cast<int32_t>(surface->h)}};
    }

    return rd;
}

//=============================================================================
// Rendering
//=============================================================================

bool YDrawRenderMethod::renderShape(tvg::RenderData data) {
    auto* rd = static_cast<YDrawRenderData*>(data);
    if (!rd || !rd->valid || rd->type != YDrawRenderData::Type::Shape) {
        return false;
    }

    bool hasGradient = (rd->fill != nullptr);

    uint8_t fillA = static_cast<uint8_t>((rd->fillA * rd->opacity) / 255);
    uint8_t strokeA = static_cast<uint8_t>((rd->strokeA * rd->opacity) / 255);

    uint32_t fillColor = (fillA > 0) ? rgbaToPackedABGR(rd->fillR, rd->fillG, rd->fillB, fillA) : 0;
    uint32_t strokeColor = (strokeA > 0 && rd->strokeWidth > 0)
                               ? rgbaToPackedABGR(rd->strokeR, rd->strokeG, rd->strokeB, strokeA) : 0;

    if (fillColor == 0 && strokeColor == 0 && !hasGradient) {
        return true;
    }

    if (hasGradient && tryRenderAsGradientBox(rd)) { return true;
}

    if (tryRenderAsEllipse(rd)) { return true;
}
    if (tryRenderAsBox(rd)) { return true;
}
    if (tryRenderAsPolygon(rd)) { return true;
}

    if (tryRenderAsFilledPath(rd)) { return true;
}

    renderPath(rd);
    return true;
}

bool YDrawRenderMethod::renderImage(tvg::RenderData data) {
    auto* rd = static_cast<YDrawRenderData*>(data);
    if (!rd || !rd->valid || rd->type != YDrawRenderData::Type::Image) {
        return false;
    }

    // Image rendering not yet implemented — new yetty image pipeline lives
    // in ypaint complex primitives; wiring is a separate task.
    ywarn("YDrawRenderMethod::renderImage: not yet implemented");
    return true;
}

//=============================================================================
// Disposal
//=============================================================================

void YDrawRenderMethod::dispose(tvg::RenderData data) {
    auto* rd = static_cast<YDrawRenderData*>(data);
    if (!rd) { return;
}

    auto it = std::find(_renderDataList.begin(), _renderDataList.end(), rd);
    if (it != _renderDataList.end()) {
        _renderDataList.erase(it);
    }
    delete rd;
}

//=============================================================================
// Region/bounds
//=============================================================================

tvg::RenderRegion YDrawRenderMethod::region(tvg::RenderData data) {
    auto* rd = static_cast<YDrawRenderData*>(data);
    if (!rd) { return {{0, 0}, {0, 0}};
}
    return rd->bounds;
}

bool YDrawRenderMethod::bounds(tvg::RenderData data, tvg::Point* pt4, const tvg::Matrix& m) {
    auto* rd = static_cast<YDrawRenderData*>(data);
    if (!rd || !pt4) { return false;
}

    auto& b = rd->bounds;
    pt4[0] = {static_cast<float>(b.min.x), static_cast<float>(b.min.y)};
    pt4[1] = {static_cast<float>(b.max.x), static_cast<float>(b.min.y)};
    pt4[2] = {static_cast<float>(b.max.x), static_cast<float>(b.max.y)};
    pt4[3] = {static_cast<float>(b.min.x), static_cast<float>(b.max.y)};

    for (int i = 0; i < 4; ++i) {
        pt4[i] = transformPoint(pt4[i], m);
    }
    return true;
}

bool YDrawRenderMethod::intersectsShape(tvg::RenderData data, const tvg::RenderRegion& region) {
    auto* rd = static_cast<YDrawRenderData*>(data);
    if (!rd) { return false;
}
    return rd->bounds.intersected(region);
}

bool YDrawRenderMethod::intersectsImage(tvg::RenderData data, const tvg::RenderRegion& region) {
    return intersectsShape(data, region);
}

//=============================================================================
// Compositing (stubs)
//=============================================================================

tvg::RenderCompositor* YDrawRenderMethod::target(
    const tvg::RenderRegion& region, tvg::ColorSpace cs, tvg::CompositionFlag flags)
{
    (void)region;
    (void)cs;
    (void)flags;
    return nullptr;
}

bool YDrawRenderMethod::beginComposite(tvg::RenderCompositor* cmp, tvg::MaskMethod method, uint8_t opacity) {
    (void)cmp;
    (void)method;
    (void)opacity;
    return true;
}

bool YDrawRenderMethod::endComposite(tvg::RenderCompositor* cmp) {
    (void)cmp;
    return true;
}

//=============================================================================
// Effects (stubs)
//=============================================================================

void YDrawRenderMethod::prepare(tvg::RenderEffect* effect, const tvg::Matrix& transform) {
    (void)effect;
    (void)transform;
}

bool YDrawRenderMethod::region(tvg::RenderEffect* effect) {
    (void)effect;
    return false;
}

bool YDrawRenderMethod::render(tvg::RenderCompositor* cmp, const tvg::RenderEffect* effect, bool direct) {
    (void)cmp;
    (void)effect;
    (void)direct;
    return true;
}

void YDrawRenderMethod::dispose(tvg::RenderEffect* effect) {
    (void)effect;
}

//=============================================================================
// Misc
//=============================================================================

bool YDrawRenderMethod::blend(tvg::BlendMethod method) {
    _blendMethod = method;
    return true;
}

tvg::ColorSpace YDrawRenderMethod::colorSpace() {
    return tvg::ColorSpace::ABGR8888;
}

const tvg::RenderSurface* YDrawRenderMethod::mainSurface() {
    return nullptr;
}

void YDrawRenderMethod::damage(tvg::RenderData rd, const tvg::RenderRegion& region) {
    (void)rd;
    (void)region;
}

bool YDrawRenderMethod::partial(bool disable) {
    (void)disable;
    return false;
}

//=============================================================================
// Helper methods
//=============================================================================

tvg::Point YDrawRenderMethod::transformPoint(const tvg::Point& p, const tvg::Matrix& m) {
    return {
        m.e11 * p.x + m.e12 * p.y + m.e13,
        m.e21 * p.x + m.e22 * p.y + m.e23
    };
}

uint32_t YDrawRenderMethod::rgbaToPackedABGR(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(r);
}

//=============================================================================
// Mesh polygon emission
//
// The new ypaint core has no mesh_polygon primitive, so we approximate:
//   - fill: fan-triangulation from vertex[0] (exact for convex, approximate
//     for concave — acceptable for SVG shape-detection fallbacks)
//   - stroke: one SDF segment per edge
//=============================================================================

void YDrawRenderMethod::emitMeshPolygon(uint32_t vertexCount, const float* vertices,
                                         uint32_t fillColor, uint32_t strokeColor, float strokeWidth)
{
    if (!_buffer || vertexCount < 3 || !vertices) { return;
}

    bool hasFill = (fillColor & 0xFF000000u) != 0;
    bool hasStroke = (strokeColor & 0xFF000000u) != 0 && strokeWidth > 0.0f;

    if (hasFill) {
        for (uint32_t i = 1; i + 1 < vertexCount; ++i) {
            yetty_ysdf_triangle tri;
            tri.vertex_a_x = vertices[0];
            tri.vertex_a_y = vertices[1];
            tri.vertex_b_x = vertices[i * 2];
            tri.vertex_b_y = vertices[i * 2 + 1];
            tri.vertex_c_x = vertices[(i + 1) * 2];
            tri.vertex_c_y = vertices[(i + 1) * 2 + 1];
            auto r = yetty_ysdf_add_triangle(_buffer, 0, fillColor, 0, 0.0f, &tri);
            if (r.error == YPAINT_OK) { _nextPrimId++;
}
        }
    }

    if (hasStroke) {
        for (uint32_t i = 0; i < vertexCount; ++i) {
            uint32_t j = (i + 1) % vertexCount;
            yetty_ysdf_segment seg;
            seg.start_x = vertices[i * 2];
            seg.start_y = vertices[i * 2 + 1];
            seg.end_x = vertices[j * 2];
            seg.end_y = vertices[j * 2 + 1];
            auto r = yetty_ysdf_add_segment(_buffer, 0, 0, strokeColor, strokeWidth, &seg);
            if (r.error == YPAINT_OK) { _nextPrimId++;
}
        }
    }
}

void YDrawRenderMethod::emitMeshPolygonGroup(uint32_t totalVertexCount,
                                              uint32_t contourCount, const uint32_t* contourStarts,
                                              const float* vertices,
                                              uint32_t fillColor, uint32_t strokeColor, float strokeWidth)
{
    if (!_buffer || contourCount == 0 || !contourStarts || !vertices) { return;
}
    for (uint32_t c = 0; c < contourCount; ++c) {
        uint32_t start = contourStarts[c];
        uint32_t end = (c + 1 < contourCount) ? contourStarts[c + 1] : totalVertexCount;
        if (end <= start) { continue;
}
        uint32_t count = end - start;
        emitMeshPolygon(count, vertices + start * 2, fillColor, strokeColor, strokeWidth);
    }
}

//=============================================================================
// Shape detection (ported from thorvg-renderer.cpp)
//=============================================================================

bool YDrawRenderMethod::tryRenderAsEllipse(YDrawRenderData* rd) {
    const auto& cmds = rd->cmds;
    const auto& pts = rd->pts;
    const auto& m = rd->transform;

    // Ellipse pattern: MoveTo, CubicTo, CubicTo, CubicTo, CubicTo, Close
    if (cmds.size() != 6) { return false;
}
    if (cmds[0] != tvg::PathCommand::MoveTo) { return false;
}
    if (cmds[1] != tvg::PathCommand::CubicTo) { return false;
}
    if (cmds[2] != tvg::PathCommand::CubicTo) { return false;
}
    if (cmds[3] != tvg::PathCommand::CubicTo) { return false;
}
    if (cmds[4] != tvg::PathCommand::CubicTo) { return false;
}
    if (cmds[5] != tvg::PathCommand::Close) { return false;
}

    if (pts.size() < 13) { return false;
}

    float minX = 1e10f, minY = 1e10f, maxX = -1e10f, maxY = -1e10f;
    for (const auto& pt : pts) {
        auto tp = transformPoint(pt, m);
        minX = std::min(minX, tp.x);
        minY = std::min(minY, tp.y);
        maxX = std::max(maxX, tp.x);
        maxY = std::max(maxY, tp.y);
    }

    float cx = (minX + maxX) / 2.0f;
    float cy = (minY + maxY) / 2.0f;
    float rx = (maxX - minX) / 2.0f;
    float ry = (maxY - minY) / 2.0f;

    uint8_t fillA = static_cast<uint8_t>((rd->fillA * rd->opacity) / 255);
    uint8_t strokeA = static_cast<uint8_t>((rd->strokeA * rd->opacity) / 255);
    uint32_t fillColor = (fillA > 0) ? rgbaToPackedABGR(rd->fillR, rd->fillG, rd->fillB, fillA) : 0;
    uint32_t strokeColor = (strokeA > 0) ? rgbaToPackedABGR(rd->strokeR, rd->strokeG, rd->strokeB, strokeA) : 0;

    yetty_ysdf_ellipse geom{cx, cy, rx, ry};
    auto result = yetty_ysdf_add_ellipse(_buffer, 0, fillColor, strokeColor, rd->strokeWidth, &geom);
    if (result.error == YPAINT_OK) { _nextPrimId++;
}
    return true;
}

bool YDrawRenderMethod::tryRenderAsBox(YDrawRenderData* rd) {
    const auto& cmds = rd->cmds;
    const auto& pts = rd->pts;
    const auto& m = rd->transform;

    bool isSimpleRect4 = (cmds.size() == 5 && pts.size() == 4 &&
                          cmds[0] == tvg::PathCommand::MoveTo &&
                          cmds[1] == tvg::PathCommand::LineTo &&
                          cmds[2] == tvg::PathCommand::LineTo &&
                          cmds[3] == tvg::PathCommand::LineTo &&
                          cmds[4] == tvg::PathCommand::Close);

    bool isSimpleRect5 = (cmds.size() == 6 && pts.size() == 5 &&
                          cmds[0] == tvg::PathCommand::MoveTo &&
                          cmds[1] == tvg::PathCommand::LineTo &&
                          cmds[2] == tvg::PathCommand::LineTo &&
                          cmds[3] == tvg::PathCommand::LineTo &&
                          cmds[4] == tvg::PathCommand::LineTo &&
                          cmds[5] == tvg::PathCommand::Close);

    bool isSimpleRect = isSimpleRect4 || isSimpleRect5;

    bool isRoundedRect = (cmds.size() == 10 &&
                          cmds[0] == tvg::PathCommand::MoveTo &&
                          cmds[1] == tvg::PathCommand::LineTo &&
                          cmds[2] == tvg::PathCommand::CubicTo &&
                          cmds[3] == tvg::PathCommand::LineTo &&
                          cmds[4] == tvg::PathCommand::CubicTo &&
                          cmds[5] == tvg::PathCommand::LineTo &&
                          cmds[6] == tvg::PathCommand::CubicTo &&
                          cmds[7] == tvg::PathCommand::LineTo &&
                          cmds[8] == tvg::PathCommand::CubicTo &&
                          cmds[9] == tvg::PathCommand::Close);

    if (!isSimpleRect && !isRoundedRect) { return false;
}

    float cornerRadius = 0.0f;
    if (isRoundedRect && pts.size() >= 5) {
        auto lineEnd = transformPoint(pts[1], m);
        auto bezierCP1 = transformPoint(pts[2], m);
        float dx = bezierCP1.x - lineEnd.x;
        float dy = bezierCP1.y - lineEnd.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        cornerRadius = dist / 0.5522847f;
    }

    if (isSimpleRect) {
        uint32_t checkCount = std::min(static_cast<uint32_t>(pts.size()), 4u);
        std::vector<float> xVals(checkCount), yVals(checkCount);
        for (uint32_t i = 0; i < checkCount; ++i) {
            auto tp = transformPoint(pts[i], m);
            xVals[i] = tp.x;
            yVals[i] = tp.y;
        }
        std::sort(xVals.begin(), xVals.end());
        std::sort(yVals.begin(), yVals.end());

        const float eps = 0.5f;
        bool twoUniqueX = (checkCount == 4 &&
                           std::abs(xVals[0] - xVals[1]) < eps &&
                           std::abs(xVals[2] - xVals[3]) < eps &&
                           std::abs(xVals[1] - xVals[2]) > eps);
        bool twoUniqueY = (checkCount == 4 &&
                           std::abs(yVals[0] - yVals[1]) < eps &&
                           std::abs(yVals[2] - yVals[3]) < eps &&
                           std::abs(yVals[1] - yVals[2]) > eps);
        if (!twoUniqueX || !twoUniqueY) { return false;
}
    }

    float minX = 1e10f, minY = 1e10f, maxX = -1e10f, maxY = -1e10f;
    for (const auto& pt : pts) {
        auto tp = transformPoint(pt, m);
        minX = std::min(minX, tp.x);
        minY = std::min(minY, tp.y);
        maxX = std::max(maxX, tp.x);
        maxY = std::max(maxY, tp.y);
    }

    float cx = (minX + maxX) / 2.0f;
    float cy = (minY + maxY) / 2.0f;
    float hw = (maxX - minX) / 2.0f;
    float hh = (maxY - minY) / 2.0f;

    uint8_t fillA = static_cast<uint8_t>((rd->fillA * rd->opacity) / 255);
    uint8_t strokeA = static_cast<uint8_t>((rd->strokeA * rd->opacity) / 255);
    uint32_t fillColor = (fillA > 0) ? rgbaToPackedABGR(rd->fillR, rd->fillG, rd->fillB, fillA) : 0;
    uint32_t strokeColor = (strokeA > 0) ? rgbaToPackedABGR(rd->strokeR, rd->strokeG, rd->strokeB, strokeA) : 0;

    yetty_ysdf_box geom{cx, cy, hw, hh, cornerRadius};
    auto result = yetty_ysdf_add_box(_buffer, 0, fillColor, strokeColor, rd->strokeWidth, &geom);
    if (result.error == YPAINT_OK) { _nextPrimId++;
}
    return true;
}

bool YDrawRenderMethod::tryRenderAsPolygon(YDrawRenderData* rd) {
    const auto& cmds = rd->cmds;
    const auto& pts = rd->pts;
    const auto& m = rd->transform;

    uint8_t fillA = static_cast<uint8_t>((rd->fillA * rd->opacity) / 255);
    if (fillA == 0) { return false;
}

    bool hasCurves = false;
    bool isClosed = false;
    int moveCount = 0;

    for (const auto& cmd : cmds) {
        if (cmd == tvg::PathCommand::MoveTo) { moveCount++;
        } else if (cmd == tvg::PathCommand::CubicTo) { hasCurves = true;
        } else if (cmd == tvg::PathCommand::Close) { isClosed = true;
}
    }

    if (hasCurves || moveCount > 1 || !isClosed) { return false;
}

    std::vector<float> vertices;
    uint32_t ptIdx = 0;

    for (const auto& cmd : cmds) {
        if (ptIdx >= pts.size()) { break;
}
        if (cmd == tvg::PathCommand::MoveTo || cmd == tvg::PathCommand::LineTo) {
            auto p = transformPoint(pts[ptIdx++], m);
            vertices.push_back(p.x);
            vertices.push_back(p.y);
        }
    }

    uint32_t vertexCount = static_cast<uint32_t>(vertices.size() / 2);
    if (vertexCount < 3) { return false;
}

    uint8_t strokeA = static_cast<uint8_t>((rd->strokeA * rd->opacity) / 255);
    uint32_t fillColor = rgbaToPackedABGR(rd->fillR, rd->fillG, rd->fillB, fillA);
    uint32_t strokeColor = (strokeA > 0) ? rgbaToPackedABGR(rd->strokeR, rd->strokeG, rd->strokeB, strokeA) : 0;

    emitMeshPolygon(vertexCount, vertices.data(), fillColor, strokeColor, rd->strokeWidth);
    return true;
}

bool YDrawRenderMethod::tryRenderAsFilledPath(YDrawRenderData* rd) {
    const auto& cmds = rd->cmds;
    const auto& pts = rd->pts;
    const auto& m = rd->transform;

    uint8_t fillA = static_cast<uint8_t>((rd->fillA * rd->opacity) / 255);
    uint32_t fillColor = (fillA > 0) ? rgbaToPackedABGR(rd->fillR, rd->fillG, rd->fillB, fillA) : 0;

    if ((fillColor & 0xFF000000) == 0) {
        return false;
    }

    bool hasClose = false;
    bool hasContent = false;
    int moveCount = 0;

    for (const auto& cmd : cmds) {
        switch (cmd) {
            case tvg::PathCommand::MoveTo:
                moveCount++;
                break;
            case tvg::PathCommand::LineTo:
            case tvg::PathCommand::CubicTo:
                hasContent = true;
                break;
            case tvg::PathCommand::Close:
                hasClose = true;
                break;
        }
    }

    if (!hasClose || !hasContent) {
        return false;
    }

    constexpr float FLATTEN_TOLERANCE_SQ = 16.0f;

    std::vector<float> allVertices;
    std::vector<uint32_t> contourStarts;
    uint32_t ptIdx = 0;
    tvg::Point current = {0, 0};
    tvg::Point subpathStart = {0, 0};
    uint32_t subpathVertexStart = 0;
    bool inSubpath = false;

    for (size_t i = 0; i < cmds.size() && ptIdx < pts.size(); ++i) {
        switch (cmds[i]) {
            case tvg::PathCommand::MoveTo:
                if (ptIdx < pts.size()) {
                    subpathVertexStart = static_cast<uint32_t>(allVertices.size() / 2);
                    contourStarts.push_back(subpathVertexStart);

                    current = transformPoint(pts[ptIdx++], m);
                    subpathStart = current;
                    allVertices.push_back(current.x);
                    allVertices.push_back(current.y);
                    inSubpath = true;
                }
                break;

            case tvg::PathCommand::LineTo:
                if (ptIdx < pts.size() && inSubpath) {
                    current = transformPoint(pts[ptIdx++], m);
                    allVertices.push_back(current.x);
                    allVertices.push_back(current.y);
                }
                break;

            case tvg::PathCommand::CubicTo:
                if (ptIdx + 2 < pts.size() && inSubpath) {
                    tvg::Point cp1 = transformPoint(pts[ptIdx++], m);
                    tvg::Point cp2 = transformPoint(pts[ptIdx++], m);
                    tvg::Point end = transformPoint(pts[ptIdx++], m);

                    flattenCubicBezier(current.x, current.y,
                                      cp1.x, cp1.y,
                                      cp2.x, cp2.y,
                                      end.x, end.y,
                                      FLATTEN_TOLERANCE_SQ,
                                      allVertices);
                    current = end;
                }
                break;

            case tvg::PathCommand::Close:
                if (inSubpath) {
                    uint32_t subpathVertexEnd = static_cast<uint32_t>(allVertices.size() / 2);
                    if (subpathVertexEnd > subpathVertexStart) {
                        float lastX = allVertices[allVertices.size() - 2];
                        float lastY = allVertices[allVertices.size() - 1];
                        float firstX = allVertices[subpathVertexStart * 2];
                        float firstY = allVertices[subpathVertexStart * 2 + 1];
                        constexpr float EPS = 0.01f;
                        if (std::abs(lastX - firstX) < EPS && std::abs(lastY - firstY) < EPS) {
                            allVertices.pop_back();
                            allVertices.pop_back();
                        }
                    }
                    current = subpathStart;
                    inSubpath = false;
                }
                break;
        }
    }

    uint32_t totalVertexCount = static_cast<uint32_t>(allVertices.size() / 2);
    if (totalVertexCount < 3 || contourStarts.empty()) {
        return false;
    }

    std::vector<uint32_t> validContourStarts;
    for (size_t i = 0; i < contourStarts.size(); ++i) {
        uint32_t start = contourStarts[i];
        uint32_t end = (i + 1 < contourStarts.size()) ? contourStarts[i + 1] : totalVertexCount;
        uint32_t count = end - start;
        if (count >= 3) {
            validContourStarts.push_back(start);
        }
    }

    if (validContourStarts.empty()) {
        return false;
    }

    uint8_t strokeA = static_cast<uint8_t>((rd->strokeA * rd->opacity) / 255);
    uint32_t strokeColor = (strokeA > 0) ? rgbaToPackedABGR(rd->strokeR, rd->strokeG, rd->strokeB, strokeA) : 0;

    if (validContourStarts.size() == 1) {
        emitMeshPolygon(totalVertexCount, allVertices.data(),
                        fillColor, strokeColor, rd->strokeWidth);
    } else {
        emitMeshPolygonGroup(totalVertexCount,
                             static_cast<uint32_t>(validContourStarts.size()),
                             validContourStarts.data(),
                             allVertices.data(),
                             fillColor, strokeColor, rd->strokeWidth);
    }
    return true;
}

void YDrawRenderMethod::renderPath(YDrawRenderData* rd) {
    const auto& cmds = rd->cmds;
    const auto& pts = rd->pts;
    const auto& m = rd->transform;

    uint8_t strokeA = static_cast<uint8_t>((rd->strokeA * rd->opacity) / 255);
    uint32_t strokeColor = (strokeA > 0 && rd->strokeWidth > 0)
                               ? rgbaToPackedABGR(rd->strokeR, rd->strokeG, rd->strokeB, strokeA) : 0;

    if (strokeColor == 0) { return;
}

    bool hasDash = !rd->dashPattern.empty() && rd->dashPattern.size() >= 2;
    float dashPos = rd->dashOffset;
    uint32_t dashIdx = 0;
    bool dashVisible = true;

    tvg::Point current{0, 0};
    uint32_t ptIdx = 0;

    for (const auto& cmd : cmds) {
        if (ptIdx >= pts.size()) { break;
}

        switch (cmd) {
            case tvg::PathCommand::MoveTo:
                current = transformPoint(pts[ptIdx++], m);
                dashPos = rd->dashOffset;
                dashIdx = 0;
                dashVisible = true;
                break;

            case tvg::PathCommand::LineTo: {
                auto next = transformPoint(pts[ptIdx++], m);
                if (hasDash) {
                    renderDashedSegment(current.x, current.y, next.x, next.y,
                                        strokeColor, rd->strokeWidth, rd->dashPattern,
                                        dashPos, dashIdx, dashVisible);
                } else {
                    yetty_ysdf_segment seg{current.x, current.y, next.x, next.y};
                    auto r = yetty_ysdf_add_segment(_buffer, 0, 0, strokeColor, rd->strokeWidth, &seg);
                    if (r.error == YPAINT_OK) { _nextPrimId++;
}
                }
                current = next;
                break;
            }

            case tvg::PathCommand::CubicTo: {
                if (ptIdx + 2 >= pts.size()) { break;
}
                auto cp1 = transformPoint(pts[ptIdx++], m);
                auto cp2 = transformPoint(pts[ptIdx++], m);
                auto end = transformPoint(pts[ptIdx++], m);

                // No bezier SDF primitive in ypaint core — flatten to segments.
                // TODO: dashed beziers would need per-segment dash state; for
                // now the flattened segments are emitted solid.
                std::vector<float> flat;
                flat.push_back(current.x);
                flat.push_back(current.y);
                flattenCubicBezier(current.x, current.y, cp1.x, cp1.y,
                                   cp2.x, cp2.y, end.x, end.y,
                                   1.0f, flat);
                for (size_t k = 2; k + 1 < flat.size(); k += 2) {
                    yetty_ysdf_segment seg{flat[k - 2], flat[k - 1], flat[k], flat[k + 1]};
                    auto r = yetty_ysdf_add_segment(_buffer, 0, 0, strokeColor, rd->strokeWidth, &seg);
                    if (r.error == YPAINT_OK) { _nextPrimId++;
}
                }
                current = end;
                break;
            }

            case tvg::PathCommand::Close:
                break;
        }
    }
}

void YDrawRenderMethod::renderDashedSegment(float x0, float y0, float x1, float y1,
                                             uint32_t strokeColor, float strokeWidth,
                                             const std::vector<float>& dashPattern,
                                             float& dashPos, uint32_t& dashIdx, bool& dashVisible)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float segLen = std::sqrt(dx * dx + dy * dy);
    if (segLen < 0.001f) { return;
}

    float ux = dx / segLen;
    float uy = dy / segLen;
    float pos = 0.0f;

    while (pos < segLen) {
        float dashLen = dashPattern[dashIdx];
        float remaining = dashLen - dashPos;
        float advance = std::min(remaining, segLen - pos);

        if (advance <= 0.001f) {
            dashPos = 0.0f;
            dashIdx = (dashIdx + 1) % dashPattern.size();
            dashVisible = !dashVisible;
            continue;
        }

        if (dashVisible && advance > 0.001f) {
            float sx = x0 + ux * pos;
            float sy = y0 + uy * pos;
            float ex = x0 + ux * (pos + advance);
            float ey = y0 + uy * (pos + advance);
            yetty_ysdf_segment seg{sx, sy, ex, ey};
            auto r = yetty_ysdf_add_segment(_buffer, 0, 0, strokeColor, strokeWidth, &seg);
            if (r.error == YPAINT_OK) { _nextPrimId++;
}
        }

        pos += advance;
        dashPos += advance;

        if (dashPos >= dashLen - 0.001f) {
            dashPos = 0.0f;
            dashIdx = (dashIdx + 1) % dashPattern.size();
            dashVisible = !dashVisible;
        }
    }
}

//=============================================================================
// Gradient helpers
//=============================================================================

bool YDrawRenderMethod::extractGradientInfo(YDrawRenderData* rd, bool& isLinear,
                                            float& gx1, float& gy1, float& gx2, float& gy2,
                                            float& gcx, float& gcy, float& gr,
                                            uint32_t& color1, uint32_t& color2)
{
    if (!rd->fill) { return false;
}

    auto fillType = rd->fill->type();
    const tvg::Fill::ColorStop* stops = nullptr;
    uint32_t stopCount = rd->fill->colorStops(&stops);

    if (stopCount < 2 || !stops) { return false;
}

    const auto& c1 = stops[0];
    const auto& c2 = stops[stopCount - 1];

    uint8_t a1 = static_cast<uint8_t>((c1.a * rd->opacity) / 255);
    uint8_t a2 = static_cast<uint8_t>((c2.a * rd->opacity) / 255);

    color1 = rgbaToPackedABGR(c1.r, c1.g, c1.b, a1);
    color2 = rgbaToPackedABGR(c2.r, c2.g, c2.b, a2);

    if (fillType == tvg::Type::LinearGradient) {
        isLinear = true;
        auto* lg = static_cast<tvg::LinearGradient*>(rd->fill);
        lg->linear(&gx1, &gy1, &gx2, &gy2);
        return true;
    } else if (fillType == tvg::Type::RadialGradient) {
        isLinear = false;
        auto* rg = static_cast<tvg::RadialGradient*>(rd->fill);
        rg->radial(&gcx, &gcy, &gr);
        return true;
    }

    return false;
}

bool YDrawRenderMethod::tryRenderAsGradientBox(YDrawRenderData* rd) {
    const auto& cmds = rd->cmds;
    const auto& pts = rd->pts;
    const auto& m = rd->transform;

    if (!rd->fill) { return false;
}

    if (cmds.size() < 4 || cmds.size() > 6) { return false;
}
    if (cmds[0] != tvg::PathCommand::MoveTo) { return false;
}
    if (cmds.back() != tvg::PathCommand::Close) { return false;
}

    for (size_t i = 1; i < cmds.size() - 1; ++i) {
        if (cmds[i] != tvg::PathCommand::LineTo) { return false;
}
    }

    if (pts.size() < 4) { return false;
}

    bool isLinear = false;
    float gx1 = 0, gy1 = 0, gx2 = 0, gy2 = 0;
    float gcx = 0, gcy = 0, gr = 0;
    uint32_t gradColor1 = 0, gradColor2 = 0;

    if (!extractGradientInfo(rd, isLinear, gx1, gy1, gx2, gy2, gcx, gcy, gr, gradColor1, gradColor2)) {
        return false;
    }
    (void)gradColor2;  // ypaint core has no gradient SDF yet — only first stop used.

    float minX = 1e10f, minY = 1e10f, maxX = -1e10f, maxY = -1e10f;
    for (size_t i = 0; i < std::min(pts.size(), size_t(4)); ++i) {
        auto tp = transformPoint(pts[i], m);
        minX = std::min(minX, tp.x);
        minY = std::min(minY, tp.y);
        maxX = std::max(maxX, tp.x);
        maxY = std::max(maxY, tp.y);
    }

    float cx = (minX + maxX) / 2.0f;
    float cy = (minY + maxY) / 2.0f;
    float hw = (maxX - minX) / 2.0f;
    float hh = (maxY - minY) / 2.0f;

    uint8_t strokeA = static_cast<uint8_t>((rd->strokeA * rd->opacity) / 255);
    uint32_t strokeColor = (strokeA > 0) ? rgbaToPackedABGR(rd->strokeR, rd->strokeG, rd->strokeB, strokeA) : 0;

    // Fallback: ypaint core currently has no gradient primitives. Emit a solid
    // shape using the first color stop — geometry is preserved, gradient is
    // flattened. Replace with a proper gradient SDF when available.
    if (isLinear) {
        yetty_ysdf_box geom{cx, cy, hw, hh, 0.0f};
        auto result = yetty_ysdf_add_box(_buffer, 0, gradColor1, strokeColor, rd->strokeWidth, &geom);
        if (result.error == YPAINT_OK) { _nextPrimId++;
}
    } else {
        float r = std::max(hw, hh);
        yetty_ysdf_ellipse geom{cx, cy, r, r};
        auto result = yetty_ysdf_add_ellipse(_buffer, 0, gradColor1, strokeColor, rd->strokeWidth, &geom);
        if (result.error == YPAINT_OK) { _nextPrimId++;
}
    }

    return true;
}

} // namespace yetty::ythorvg
