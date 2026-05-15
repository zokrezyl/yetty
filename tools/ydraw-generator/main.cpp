// ydraw-generator: Generate random shapes and output as OSC sequence
//
// Generates random 2D SDF primitives (circles, boxes, ellipses, triangles,
// segments) within specified boundaries and outputs an OSC sequence for
// ydraw or ydraw cards.

#include "../../src/yetty/ycat/osc.h"
#include "../../src/yetty/ydraw/ydraw-buffer.h"

#include <args.hxx>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>

using namespace yetty;

//=============================================================================
// Configuration
//=============================================================================

struct GeneratorConfig {
  uint32_t count = 10;
  std::string mode = "ydraw";
  float minX = 0.0f;
  float minY = 0.0f;
  float maxX = 800.0f;
  float maxY = 600.0f;
  float minSizePct = 0.5f;   // Min shape size as % of scene
  float maxSizePct = 40.0f;  // Max shape size as % of scene
  uint32_t seed = 0;
  bool opaque = false;       // Use fully opaque colors (alpha=0xFF)
  bool noStroke = false;     // Disable stroke on shapes
  bool boundsSpecified = false;  // Whether -b was explicitly provided
  bool stretch = false;      // Stretch to fill card (no aspect ratio preservation)
  float padding = -1.0f;     // Scene padding as fraction (-1 = use default 0.05)
  float paddingX = -1.0f;    // Scene padding X (-1 = use --padding or default)
  float paddingY = -1.0f;    // Scene padding Y (-1 = use --padding or default)
  // Card placement (in terminal cells)
  int cardX = -1;            // -1 = not specified
  int cardY = -1;
  int cardW = -1;
  int cardH = -1;
};

//=============================================================================
// Random shape generation
//=============================================================================

class ShapeGenerator {
public:
  explicit ShapeGenerator(const GeneratorConfig &config) : _config(config) {
    if (config.seed == 0) {
      auto s =
          std::chrono::high_resolution_clock::now().time_since_epoch().count();
      _rng.seed(static_cast<uint32_t>(s));
    } else {
      _rng.seed(config.seed);
    }
  }

  void generate(std::shared_ptr<YDrawBuffer> buffer) {
    for (uint32_t i = 0; i < _config.count; ++i) {
      addRandomShape(buffer);
    }

    if (_config.boundsSpecified) {
      buffer->setSceneBounds(_config.minX, _config.minY, _config.maxX,
                             _config.maxY);
    }
    buffer->setBgColor(randomColor(0xFF));
  }

private:
  GeneratorConfig _config;
  std::mt19937 _rng;

  float randomFloat(float min, float max) {
    return std::uniform_real_distribution<float>(min, max)(_rng);
  }

  uint32_t randomUint(uint32_t min, uint32_t max) {
    return std::uniform_int_distribution<uint32_t>(min, max)(_rng);
  }

  uint32_t randomColor(uint8_t minAlpha = 0x80) {
    uint8_t r = static_cast<uint8_t>(randomUint(0, 255));
    uint8_t g = static_cast<uint8_t>(randomUint(0, 255));
    uint8_t b = static_cast<uint8_t>(randomUint(0, 255));
    uint8_t a = _config.opaque ? 0xFF : static_cast<uint8_t>(randomUint(minAlpha, 255));
    return (a << 24) | (b << 16) | (g << 8) | r; // ABGR format
  }

  float sceneWidth() const { return _config.maxX - _config.minX; }
  float sceneHeight() const { return _config.maxY - _config.minY; }

  // Generate size with exponential-like distribution for variety
  // Returns sizes from minSize to maxSize with bias toward smaller values
  float randomSize(float minSize, float maxSize) {
    // Use exponential distribution: more small shapes, fewer large ones
    float t = randomFloat(0.0f, 1.0f);
    t = t * t * t;  // Cubic bias toward small
    return minSize + t * (maxSize - minSize);
  }

  void addRandomShape(std::shared_ptr<YDrawBuffer> buffer) {
    uint32_t shapeType = randomUint(0, 24);
    uint32_t layer = randomUint(0, 10);
    uint32_t fillColor = randomColor();
    uint32_t strokeColor = _config.noStroke ? 0 : randomColor();
    float strokeWidth = _config.noStroke ? 0.0f : randomFloat(0.0f, 3.0f);

    switch (shapeType) {
    case 0:  addCircle(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 1:  addBox(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 2:  addEllipse(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 3:  addTriangle(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 4:  addSegment(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 5:  addRoundedBox(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 6:  addRhombus(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 7:  addPentagon(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 8:  addHexagon(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 9:  addStar(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 10: addHeart(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 11: addCross(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 12: addCapsule(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 13: addMoon(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 14: addEgg(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 15: addTrapezoid(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 16: addParallelogram(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 17: addOctogon(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 18: addHexagram(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 19: addPentagram(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 20: addPie(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 21: addRing(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 22: addVesica(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 23: addBezier2(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    case 24: addArc(buffer, layer, fillColor, strokeColor, strokeWidth); break;
    }
  }

  void addCircle(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                 uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float radius = randomSize(minR, maxR);
    float cx = randomFloat(_config.minX + radius, _config.maxX - radius);
    float cy = randomFloat(_config.minY + radius, _config.maxY - radius);

    buffer->addCircle(layer, cx, cy, radius, fillColor, strokeColor,
                      strokeWidth, 0.0f);
  }

  void addBox(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
              uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minH = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxH = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float hw = randomSize(minH, maxH);
    float hh = randomSize(minH, maxH);
    float cx = randomFloat(_config.minX + hw, _config.maxX - hw);
    float cy = randomFloat(_config.minY + hh, _config.maxY - hh);
    float round = randomFloat(0.0f, std::min(hw, hh) * 0.5f);

    buffer->addBox(layer, cx, cy, hw, hh, fillColor, strokeColor, strokeWidth,
                   round);
  }

  void addEllipse(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                  uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float rx = randomSize(minR, maxR);
    float ry = randomSize(minR, maxR);
    float cx = randomFloat(_config.minX + rx, _config.maxX - rx);
    float cy = randomFloat(_config.minY + ry, _config.maxY - ry);

    buffer->addEllipse(layer, cx, cy, rx, ry, fillColor, strokeColor,
                       strokeWidth, 0.0f);
  }

  void addTriangle(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                   uint32_t fillColor, uint32_t strokeColor,
                   float strokeWidth) {
    // Generate triangle of varying size around a center point
    float minSize = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxSize = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float size = randomSize(minSize, maxSize);
    
    float cx = randomFloat(_config.minX + size, _config.maxX - size);
    float cy = randomFloat(_config.minY + size, _config.maxY - size);
    
    // Random triangle vertices around center
    float ax = cx + randomFloat(-size, size);
    float ay = cy + randomFloat(-size, size);
    float bx = cx + randomFloat(-size, size);
    float by = cy + randomFloat(-size, size);
    float vx = cx + randomFloat(-size, size);
    float vy = cy + randomFloat(-size, size);

    buffer->addTriangle(layer, ax, ay, bx, by, vx, vy, fillColor, strokeColor,
                        strokeWidth, 0.0f);
  }

  void addSegment(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                  uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    // Segment length varies from tiny to large
    float minLen = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxLen = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float len = randomSize(minLen, maxLen);
    
    float cx = randomFloat(_config.minX + len, _config.maxX - len);
    float cy = randomFloat(_config.minY + len, _config.maxY - len);
    float angle = randomFloat(0.0f, 6.28318f);
    
    float x0 = cx - std::cos(angle) * len * 0.5f;
    float y0 = cy - std::sin(angle) * len * 0.5f;
    float x1 = cx + std::cos(angle) * len * 0.5f;
    float y1 = cy + std::sin(angle) * len * 0.5f;

    // Segments typically use stroke, not fill
    buffer->addSegment(layer, x0, y0, x1, y1, 0, strokeColor,
                       strokeWidth + 0.5f, 0.0f);
  }

  void addRoundedBox(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                     uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minH = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxH = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float hw = randomSize(minH, maxH);
    float hh = randomSize(minH, maxH);
    float cx = randomFloat(_config.minX + hw, _config.maxX - hw);
    float cy = randomFloat(_config.minY + hh, _config.maxY - hh);
    float r0 = randomFloat(0.0f, std::min(hw, hh) * 0.8f);
    float r1 = randomFloat(0.0f, std::min(hw, hh) * 0.8f);
    float r2 = randomFloat(0.0f, std::min(hw, hh) * 0.8f);
    float r3 = randomFloat(0.0f, std::min(hw, hh) * 0.8f);
    buffer->addRoundedBox(layer, cx, cy, hw, hh, r0, r1, r2, r3, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addRhombus(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                  uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float bx = randomSize(minR, maxR);
    float by = randomSize(minR, maxR);
    float cx = randomFloat(_config.minX + bx, _config.maxX - bx);
    float cy = randomFloat(_config.minY + by, _config.maxY - by);
    buffer->addRhombus(layer, cx, cy, bx, by, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addPentagon(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                   uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float r = randomSize(minR, maxR);
    float cx = randomFloat(_config.minX + r, _config.maxX - r);
    float cy = randomFloat(_config.minY + r, _config.maxY - r);
    buffer->addPentagon(layer, cx, cy, r, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addHexagon(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                  uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float r = randomSize(minR, maxR);
    float cx = randomFloat(_config.minX + r, _config.maxX - r);
    float cy = randomFloat(_config.minY + r, _config.maxY - r);
    buffer->addHexagon(layer, cx, cy, r, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addStar(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
               uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float r = randomSize(minR, maxR);
    float cx = randomFloat(_config.minX + r, _config.maxX - r);
    float cy = randomFloat(_config.minY + r, _config.maxY - r);
    float n = static_cast<float>(randomUint(5, 8));  // 5-8 points
    float m = randomFloat(2.0f, n - 1.0f);           // inner ratio
    buffer->addStar(layer, cx, cy, r, n, m, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addHeart(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minS = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxS = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float scale = randomSize(minS, maxS);
    float cx = randomFloat(_config.minX + scale, _config.maxX - scale);
    float cy = randomFloat(_config.minY + scale, _config.maxY - scale);
    buffer->addHeart(layer, cx, cy, scale, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addCross(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float size = randomSize(minR, maxR);
    float cx = randomFloat(_config.minX + size, _config.maxX - size);
    float cy = randomFloat(_config.minY + size, _config.maxY - size);
    float bx = size * randomFloat(0.6f, 1.0f);
    float by = size * randomFloat(0.2f, 0.4f);
    float r = size * randomFloat(0.05f, 0.2f);
    buffer->addCross(layer, cx, cy, bx, by, r, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addCapsule(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                  uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float len = randomSize(minR, maxR);
    float r = len * randomFloat(0.1f, 0.4f);
    float cx = randomFloat(_config.minX + len, _config.maxX - len);
    float cy = randomFloat(_config.minY + len, _config.maxY - len);
    float angle = randomFloat(0.0f, 6.28318f);
    float ax = cx - std::cos(angle) * len * 0.5f;
    float ay = cy - std::sin(angle) * len * 0.5f;
    float bx = cx + std::cos(angle) * len * 0.5f;
    float by = cy + std::sin(angle) * len * 0.5f;
    buffer->addCapsule(layer, ax, ay, bx, by, r, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addMoon(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
               uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float ra = randomSize(minR, maxR);
    float rb = ra * randomFloat(0.5f, 0.9f);
    float d = ra * randomFloat(0.2f, 0.8f);
    float cx = randomFloat(_config.minX + ra, _config.maxX - ra);
    float cy = randomFloat(_config.minY + ra, _config.maxY - ra);
    buffer->addMoon(layer, cx, cy, d, ra, rb, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addEgg(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
              uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float ra = randomSize(minR, maxR);
    float rb = ra * randomFloat(0.4f, 0.8f);
    float cx = randomFloat(_config.minX + ra, _config.maxX - ra);
    float cy = randomFloat(_config.minY + ra, _config.maxY - ra);
    buffer->addEgg(layer, cx, cy, ra, rb, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addTrapezoid(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                    uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float size = randomSize(minR, maxR);
    float r1 = size * randomFloat(0.3f, 0.8f);
    float r2 = size * randomFloat(0.5f, 1.0f);
    float he = size * randomFloat(0.5f, 1.0f);
    float cx = randomFloat(_config.minX + size, _config.maxX - size);
    float cy = randomFloat(_config.minY + size, _config.maxY - size);
    buffer->addTrapezoid(layer, cx, cy, r1, r2, he, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addParallelogram(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                        uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float size = randomSize(minR, maxR);
    float wi = size * randomFloat(0.5f, 1.0f);
    float he = size * randomFloat(0.3f, 0.8f);
    float sk = size * randomFloat(0.1f, 0.5f);
    float cx = randomFloat(_config.minX + size, _config.maxX - size);
    float cy = randomFloat(_config.minY + size, _config.maxY - size);
    buffer->addParallelogram(layer, cx, cy, wi, he, sk, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addOctogon(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                  uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float r = randomSize(minR, maxR);
    float cx = randomFloat(_config.minX + r, _config.maxX - r);
    float cy = randomFloat(_config.minY + r, _config.maxY - r);
    buffer->addOctogon(layer, cx, cy, r, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addHexagram(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                   uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float r = randomSize(minR, maxR);
    float cx = randomFloat(_config.minX + r, _config.maxX - r);
    float cy = randomFloat(_config.minY + r, _config.maxY - r);
    buffer->addHexagram(layer, cx, cy, r, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addPentagram(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                    uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float r = randomSize(minR, maxR);
    float cx = randomFloat(_config.minX + r, _config.maxX - r);
    float cy = randomFloat(_config.minY + r, _config.maxY - r);
    buffer->addPentagram(layer, cx, cy, r, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addPie(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
              uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float r = randomSize(minR, maxR);
    float cx = randomFloat(_config.minX + r, _config.maxX - r);
    float cy = randomFloat(_config.minY + r, _config.maxY - r);
    float angle = randomFloat(0.3f, 2.5f);
    float sc_x = std::sin(angle);
    float sc_y = std::cos(angle);
    buffer->addPie(layer, cx, cy, sc_x, sc_y, r, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addRing(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
               uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float r = randomSize(minR, maxR);
    float th = r * randomFloat(0.1f, 0.4f);
    float cx = randomFloat(_config.minX + r, _config.maxX - r);
    float cy = randomFloat(_config.minY + r, _config.maxY - r);
    float angle = randomFloat(0.0f, 6.28318f);
    float nx = std::cos(angle);
    float ny = std::sin(angle);
    buffer->addRing(layer, cx, cy, nx, ny, r, th, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addVesica(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                 uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float size = randomSize(minR, maxR);
    float w = size * randomFloat(0.3f, 0.8f);
    float h = size;
    float cx = randomFloat(_config.minX + size, _config.maxX - size);
    float cy = randomFloat(_config.minY + size, _config.maxY - size);
    buffer->addVesica(layer, cx, cy, w, h, fillColor, strokeColor, strokeWidth, 0.0f);
  }

  void addBezier2(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
                  uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float size = randomSize(minR, maxR);
    float cx = randomFloat(_config.minX + size, _config.maxX - size);
    float cy = randomFloat(_config.minY + size, _config.maxY - size);
    float ax = cx + randomFloat(-size, size);
    float ay = cy + randomFloat(-size, size);
    float bx = cx + randomFloat(-size, size);
    float by = cy + randomFloat(-size, size);
    float ccx = cx + randomFloat(-size, size);
    float ccy = cy + randomFloat(-size, size);
    buffer->addBezier2(layer, ax, ay, bx, by, ccx, ccy, 0, strokeColor, strokeWidth + 1.0f, 0.0f);
  }

  void addArc(std::shared_ptr<YDrawBuffer> buffer, uint32_t layer,
              uint32_t fillColor, uint32_t strokeColor, float strokeWidth) {
    float minR = std::min(sceneWidth(), sceneHeight()) * _config.minSizePct / 100.0f;
    float maxR = std::min(sceneWidth(), sceneHeight()) * _config.maxSizePct / 100.0f;
    float ra = randomSize(minR, maxR);
    float rb = ra * randomFloat(0.1f, 0.3f);
    float cx = randomFloat(_config.minX + ra, _config.maxX - ra);
    float cy = randomFloat(_config.minY + ra, _config.maxY - ra);
    float angle = randomFloat(0.3f, 2.5f);
    float sc_x = std::sin(angle);
    float sc_y = std::cos(angle);
    buffer->addArc(layer, cx, cy, sc_x, sc_y, ra, rb, fillColor, strokeColor, strokeWidth, 0.0f);
  }
};

//=============================================================================
// Parse bounds argument "minX,minY,maxX,maxY"
//=============================================================================

bool parseBounds(const std::string &s, float &minX, float &minY, float &maxX,
                 float &maxY) {
  float values[4];
  int idx = 0;
  size_t start = 0;

  for (size_t i = 0; i <= s.size() && idx < 4; ++i) {
    if (i == s.size() || s[i] == ',') {
      if (i > start) {
        try {
          values[idx++] = std::stof(s.substr(start, i - start));
        } catch (...) {
          return false;
        }
      }
      start = i + 1;
    }
  }

  if (idx != 4)
    return false;

  minX = values[0];
  minY = values[1];
  maxX = values[2];
  maxY = values[3];

  return minX < maxX && minY < maxY;
}

//=============================================================================
// Main
//=============================================================================

int main(int argc, char **argv) {
  args::ArgumentParser parser(
      "ydraw-generator - Generate random shapes as OSC sequence",
      "Outputs an OSC sequence for ydraw/ydraw cards.");
  args::HelpFlag help(parser, "help", "Show help", {"help"});

  args::ValueFlag<uint32_t> countFlag(
      parser, "N", "Number of shapes to generate", {'n', "count"}, 10);
  args::ValueFlag<std::string> modeFlag(
      parser, "MODE", "Output mode: ydraw or ydraw", {'m', "mode"}, "ydraw");
  args::ValueFlag<std::string> boundsFlag(parser, "BOUNDS",
                                          "Bounds as minX,minY,maxX,maxY",
                                          {'b', "bounds"}, "0,0,800,600");
  args::ValueFlag<float> minSizeFlag(
      parser, "PCT", "Min shape size as % of scene (default 0.5)", {"min"}, 0.5f);
  args::ValueFlag<float> maxSizeFlag(
      parser, "PCT", "Max shape size as % of scene (default 40)", {"max"}, 40.0f);
  args::ValueFlag<uint32_t> seedFlag(
      parser, "SEED", "Random seed (0 = use time)", {'s', "seed"}, 0);
  args::Flag opaqueFlag(
      parser, "opaque", "Use fully opaque colors (alpha=0xFF)", {"opaque"});
  args::Flag noStrokeFlag(
      parser, "no-stroke", "Disable stroke on shapes", {"no-stroke"});
  args::Flag stretchFlag(
      parser, "stretch", "Stretch to fill card (no aspect ratio preservation)", {"stretch"});
  args::ValueFlag<int> cardXFlag(
      parser, "X", "Card X position in cells", {'x'});
  args::ValueFlag<int> cardYFlag(
      parser, "Y", "Card Y position in cells", {'y'});
  args::ValueFlag<int> cardWFlag(
      parser, "W", "Card width in cells", {'w'});
  args::ValueFlag<int> cardHFlag(
      parser, "H", "Card height in cells", {'h'});
  args::ValueFlag<float> paddingFlag(
      parser, "P", "Scene padding as fraction (default 0.05)", {"padding"});
  args::ValueFlag<float> paddingXFlag(
      parser, "PX", "Scene padding X as fraction", {"padding-x"});
  args::ValueFlag<float> paddingYFlag(
      parser, "PY", "Scene padding Y as fraction", {"padding-y"});

  try {
    parser.ParseCLI(argc, argv);
  } catch (const args::Help &) {
    std::cout << parser;
    return 0;
  } catch (const args::ParseError &e) {
    std::cerr << e.what() << "\n" << parser;
    return 1;
  }

  GeneratorConfig config;
  config.count = args::get(countFlag);
  config.mode = args::get(modeFlag);
  config.seed = args::get(seedFlag);
  config.minSizePct = args::get(minSizeFlag);
  config.maxSizePct = args::get(maxSizeFlag);
  config.opaque = opaqueFlag;
  config.noStroke = noStrokeFlag;
  config.stretch = stretchFlag;
  if (cardXFlag) config.cardX = args::get(cardXFlag);
  if (cardYFlag) config.cardY = args::get(cardYFlag);
  if (cardWFlag) config.cardW = args::get(cardWFlag);
  if (cardHFlag) config.cardH = args::get(cardHFlag);
  if (paddingFlag) config.padding = args::get(paddingFlag);
  if (paddingXFlag) config.paddingX = args::get(paddingXFlag);
  if (paddingYFlag) config.paddingY = args::get(paddingYFlag);

  // Validate mode
  if (config.mode != "ydraw" && config.mode != "ydraw") {
    std::cerr << "Error: mode must be 'ydraw' or 'ydraw'\n";
    return 1;
  }

  // Validate size range
  if (config.minSizePct <= 0 || config.maxSizePct <= 0 || 
      config.minSizePct > config.maxSizePct) {
    std::cerr << "Error: --min must be > 0 and <= --max\n";
    return 1;
  }

  // Parse bounds (only if explicitly specified)
  if (boundsFlag) {
    std::string boundsStr = args::get(boundsFlag);
    if (!parseBounds(boundsStr, config.minX, config.minY, config.maxX,
                     config.maxY)) {
      std::cerr << "Error: invalid bounds format. Use: minX,minY,maxX,maxY\n";
      return 1;
    }
    config.boundsSpecified = true;
  }

  // Create buffer and generate shapes
  auto bufferResult = YDrawBuffer::create();
  if (!bufferResult) {
    std::cerr << "Failed to create YDrawBuffer\n";
    return 1;
  }
  auto buffer = *bufferResult;

  ShapeGenerator generator(config);
  generator.generate(buffer);

  // Serialize to binary
  auto binary = buffer->serialize();

  // Create OSC sequence
  auto b64 = ycat::base64Encode(binary);

  // Build OSC sequence
  // Format: ESC ] 666666 ; run -c <card> [-x X] [-y Y] [-w W] [-h H] ; <card-args> ; <base64> ESC
  std::string osc;
  osc += "\033]666666;run -c ";
  osc += config.mode;
  if (config.cardX >= 0) osc += " -x " + std::to_string(config.cardX);
  if (config.cardY >= 0) osc += " -y " + std::to_string(config.cardY);
  if (config.cardW >= 0) osc += " -w " + std::to_string(config.cardW);
  if (config.cardH >= 0) osc += " -h " + std::to_string(config.cardH);
  osc += ";";  // End generic-args, start card-args
  std::string cardArgs;
  if (config.stretch) cardArgs += "--stretch ";
  if (config.padding >= 0) cardArgs += "--padding " + std::to_string(config.padding) + " ";
  if (config.paddingX >= 0) cardArgs += "--padding-x " + std::to_string(config.paddingX) + " ";
  if (config.paddingY >= 0) cardArgs += "--padding-y " + std::to_string(config.paddingY) + " ";
  // Trim trailing space
  if (!cardArgs.empty() && cardArgs.back() == ' ') cardArgs.pop_back();
  osc += cardArgs;
  osc += ";";  // End card-args, start payload
  osc += b64;
  osc += "\033\\";

  // Output (wrapped for tmux if needed)
  std::string output = ycat::maybeWrapForTmux(osc);
  std::cout << output;

  return 0;
}
