#pragma once

/*
 * YDrawRenderMethod — ThorVG RenderMethod that emits ydraw SDF primitives
 * into a yetty_ydraw_draw_list.
 *
 * Ported from yetty-poc/src/yetty/ythorvg/ydraw-render-method.h with the
 * YDrawBuffer dependency replaced by yetty_ydraw_draw_list + ysdf
 * add helpers. The detection/flattening/gradient logic is preserved.
 */

// ThorVG public API first (for FilterMethod, etc.)
#include <thorvg.h>

// ThorVG internal header for RenderMethod
#include "tvgRender.h"

#include <cstdint>
#include <memory>
#include <vector>

struct yetty_ydraw_draw_list;

namespace yetty::ythorvg
{

//=============================================================================
// YDrawRenderData - stores shape data between prepare() and renderShape()
//=============================================================================
struct YDrawRenderData {
    enum class Type { Shape, Image };
    Type type = Type::Shape;

    // Path data (copied from RenderShape)
    std::vector<tvg::PathCommand> cmds;
    std::vector<tvg::Point> pts;

    // Transform (world space - already accumulated by ThorVG)
    tvg::Matrix transform = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    // Opacity (already accumulated by ThorVG)
    uint8_t opacity = 255;

    // Fill
    uint8_t fillR = 0, fillG = 0, fillB = 0, fillA = 0;
    tvg::Fill *fill = nullptr; // gradient (borrowed, don't delete)
    tvg::FillRule rule = tvg::FillRule::NonZero;

    // Stroke
    float strokeWidth = 0.0f;
    uint8_t strokeR = 0, strokeG = 0, strokeB = 0, strokeA = 0;
    tvg::Fill *strokeFill = nullptr;
    std::vector<float> dashPattern;
    float dashOffset = 0.0f;
    tvg::StrokeCap strokeCap = tvg::StrokeCap::Square;
    tvg::StrokeJoin strokeJoin = tvg::StrokeJoin::Bevel;

    // Image data (for Type::Image)
    tvg::RenderSurface *surface = nullptr;

    // Computed bounds
    tvg::RenderRegion bounds = {{0, 0}, {0, 0}};
    bool valid = false;
};

//=============================================================================
// YDrawRenderMethod - ThorVG RenderMethod that emits ydraw SDF primitives
//=============================================================================
class YDrawRenderMethod : public tvg::RenderMethod
{
  public:
    using Ptr = std::unique_ptr<YDrawRenderMethod>;

    // buffer is borrowed — caller retains ownership and must outlive this.
    explicit YDrawRenderMethod(yetty_ydraw_draw_list *buffer);
    ~YDrawRenderMethod() override;

    // Set target viewport size
    void setTarget(uint32_t width, uint32_t height);

    //=========================================================================
    // RenderMethod interface
    //=========================================================================

    // Lifecycle
    bool preUpdate() override;
    bool postUpdate() override;
    bool preRender() override;
    bool postRender() override;
    bool clear() override;
    bool sync() override;

    // Shape preparation - stores data for later rendering
    tvg::RenderData prepare(const tvg::RenderShape &rshape, tvg::RenderData data,
                            const tvg::Matrix &transform, tvg::Array<tvg::RenderData> &clips,
                            uint8_t opacity, tvg::RenderUpdateFlag flags, bool clipper) override;

    // Image preparation (v1.0.1 signature - no FilterMethod)
    tvg::RenderData prepare(tvg::RenderSurface *surface, tvg::RenderData data,
                            const tvg::Matrix &transform, tvg::Array<tvg::RenderData> &clips,
                            uint8_t opacity, tvg::RenderUpdateFlag flags) override;

    // Rendering - emits ydraw primitives
    bool renderShape(tvg::RenderData data) override;
    bool renderImage(tvg::RenderData data) override;

    // Disposal
    void dispose(tvg::RenderData data) override;

    // Region/bounds
    tvg::RenderRegion region(tvg::RenderData data) override;
    bool bounds(tvg::RenderData data, tvg::Point *pt4, const tvg::Matrix &m) override;
    bool intersectsShape(tvg::RenderData data, const tvg::RenderRegion &region) override;
    bool intersectsImage(tvg::RenderData data, const tvg::RenderRegion &region) override;

    // Compositing (stub for now)
    tvg::RenderCompositor *target(const tvg::RenderRegion &region, tvg::ColorSpace cs,
                                  tvg::CompositionFlag flags) override;
    bool beginComposite(tvg::RenderCompositor *cmp, tvg::MaskMethod method,
                        uint8_t opacity) override;
    bool endComposite(tvg::RenderCompositor *cmp) override;

    // Effects (stub)
    void prepare(tvg::RenderEffect *effect, const tvg::Matrix &transform) override;
    bool region(tvg::RenderEffect *effect) override;
    bool render(tvg::RenderCompositor *cmp, const tvg::RenderEffect *effect, bool direct) override;
    void dispose(tvg::RenderEffect *effect) override;

    // Misc
    bool blend(tvg::BlendMethod method) override;
    tvg::ColorSpace colorSpace() override;
    const tvg::RenderSurface *mainSurface() override;
    void damage(tvg::RenderData rd, const tvg::RenderRegion &region) override;
    bool partial(bool disable) override;

  private:
    // Shape detection and rendering helpers
    bool tryRenderAsEllipse(YDrawRenderData *rd);
    bool tryRenderAsBox(YDrawRenderData *rd);
    bool tryRenderAsGradientBox(YDrawRenderData *rd);
    bool tryRenderAsPolygon(YDrawRenderData *rd);
    bool tryRenderAsFilledPath(YDrawRenderData *rd);
    void renderPath(YDrawRenderData *rd);
    void renderDashedSegment(float x0, float y0, float x1, float y1, uint32_t strokeColor,
                             float strokeWidth, const std::vector<float> &dashPattern,
                             float &dashPos, uint32_t &dashIdx, bool &dashVisible);

    // Emit a mesh polygon: fan triangulation for fill + edges as segments for stroke.
    void emitMeshPolygon(uint32_t vertexCount, const float *vertices, uint32_t fillColor,
                         uint32_t strokeColor, float strokeWidth);
    void emitMeshPolygonGroup(uint32_t totalVertexCount, uint32_t contourCount,
                              const uint32_t *contourStarts, const float *vertices,
                              uint32_t fillColor, uint32_t strokeColor, float strokeWidth);

    // Gradient helpers
    bool extractGradientInfo(YDrawRenderData *rd, bool &isLinear, float &gx1, float &gy1,
                             float &gx2, float &gy2, float &gcx, float &gcy, float &gr,
                             uint32_t &color1, uint32_t &color2);

    // Transform helper
    tvg::Point transformPoint(const tvg::Point &p, const tvg::Matrix &m);

    // Color conversion
    uint32_t rgbaToPackedABGR(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    yetty_ydraw_draw_list *_buffer = nullptr; // borrowed
    uint32_t _width = 0;
    uint32_t _height = 0;
    uint32_t _nextPrimId = 0;
    tvg::BlendMethod _blendMethod = tvg::BlendMethod::Normal;

    // Track allocated render data for cleanup
    std::vector<YDrawRenderData *> _renderDataList;
};

} // namespace yetty::ythorvg
