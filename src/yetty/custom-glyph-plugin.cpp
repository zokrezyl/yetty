#include <yetty/custom-glyph-plugin.h>
#include <algorithm>

namespace yetty {

void CustomGlyphPlugin::addWidget(CustomGlyphWidgetPtr widget) {
    _widgets.push_back(widget);
}

void CustomGlyphPlugin::removeWidgetAt(uint32_t col, uint32_t row) {
    // Find and dispose widgets at this position first
    for (auto& widget : _widgets) {
        if (widget && widget->getCol() == col && widget->getRow() == row) {
            widget->dispose();
        }
    }

    // Then remove them
    auto it = std::remove_if(_widgets.begin(), _widgets.end(),
        [col, row](const CustomGlyphWidgetPtr& widget) {
            return !widget || (widget->getCol() == col && widget->getRow() == row);
        });

    _widgets.erase(it, _widgets.end());
}

CustomGlyphWidgetPtr CustomGlyphPlugin::getWidgetAt(uint32_t col, uint32_t row) const {
    for (const auto& widget : _widgets) {
        if (widget && widget->getCol() == col && widget->getRow() == row) {
            return widget;
        }
    }
    return nullptr;
}

void CustomGlyphPlugin::clearWidgets() {
    for (auto& widget : _widgets) {
        if (widget) {
            widget->dispose();
        }
    }
    _widgets.clear();
}

} // namespace yetty
