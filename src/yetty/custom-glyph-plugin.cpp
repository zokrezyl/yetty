#include "custom-glyph-plugin.h"
#include <algorithm>

namespace yetty {

void CustomGlyphPlugin::addLayer(CustomGlyphLayerPtr layer) {
    _layers.push_back(layer);
}

void CustomGlyphPlugin::removeLayerAt(uint32_t col, uint32_t row) {
    // Find and dispose layers at this position first
    for (auto& layer : _layers) {
        if (layer && layer->getCol() == col && layer->getRow() == row) {
            layer->dispose();
        }
    }

    // Then remove them
    auto it = std::remove_if(_layers.begin(), _layers.end(),
        [col, row](const CustomGlyphLayerPtr& layer) {
            return !layer || (layer->getCol() == col && layer->getRow() == row);
        });

    _layers.erase(it, _layers.end());
}

CustomGlyphLayerPtr CustomGlyphPlugin::getLayerAt(uint32_t col, uint32_t row) const {
    for (const auto& layer : _layers) {
        if (layer && layer->getCol() == col && layer->getRow() == row) {
            return layer;
        }
    }
    return nullptr;
}

void CustomGlyphPlugin::clearLayers() {
    for (auto& layer : _layers) {
        if (layer) {
            layer->dispose();
        }
    }
    _layers.clear();
}

} // namespace yetty
