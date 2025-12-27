#include "custom-glyph-plugin.h"
#include <algorithm>

namespace yetty {

void CustomGlyphPlugin::addLayer(CustomGlyphLayerPtr layer) {
    _layers.push_back(layer);
}

void CustomGlyphPlugin::removeLayerAt(uint32_t col, uint32_t row) {
    auto it = std::remove_if(_layers.begin(), _layers.end(),
        [col, row](const CustomGlyphLayerPtr& layer) {
            return layer->getCol() == col && layer->getRow() == row;
        });

    // Dispose removed layers
    for (auto removeIt = it; removeIt != _layers.end(); ++removeIt) {
        (*removeIt)->dispose();
    }

    _layers.erase(it, _layers.end());
}

CustomGlyphLayerPtr CustomGlyphPlugin::getLayerAt(uint32_t col, uint32_t row) const {
    for (const auto& layer : _layers) {
        if (layer->getCol() == col && layer->getRow() == row) {
            return layer;
        }
    }
    return nullptr;
}

void CustomGlyphPlugin::clearLayers() {
    for (auto& layer : _layers) {
        layer->dispose();
    }
    _layers.clear();
}

} // namespace yetty
