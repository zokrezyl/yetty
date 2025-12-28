//=============================================================================
// Plugin Layer Tests
//
// Tests for PluginLayer position, visibility, screen type, and input handling
//=============================================================================

#include <boost/ut.hpp>
#include "harness/mock_plugin.h"

using namespace boost::ut;
using namespace yetty;
using namespace yetty::test;

suite plugin_layer_tests = [] {
    "layer has default position 0,0"_test = [] {
        MockPluginLayer layer;

        expect(layer.getX() == 0_i);
        expect(layer.getY() == 0_i);
    };

    "layer position can be set"_test = [] {
        MockPluginLayer layer;

        layer.setPosition(10, 20);

        expect(layer.getX() == 10_i);
        expect(layer.getY() == 20_i);
    };

    "layer has default cell size 1x1"_test = [] {
        MockPluginLayer layer;

        expect(layer.getWidthCells() == 1_u);
        expect(layer.getHeightCells() == 1_u);
    };

    "layer cell size can be set"_test = [] {
        MockPluginLayer layer;

        layer.setCellSize(40, 20);

        expect(layer.getWidthCells() == 40_u);
        expect(layer.getHeightCells() == 20_u);
    };

    "layer pixel size can be set"_test = [] {
        MockPluginLayer layer;

        layer.setPixelSize(800, 600);

        expect(layer.getPixelWidth() == 800_u);
        expect(layer.getPixelHeight() == 600_u);
    };

    "layer is visible by default"_test = [] {
        MockPluginLayer layer;

        expect(layer.isVisible());
    };

    "layer visibility can be toggled"_test = [] {
        MockPluginLayer layer;

        layer.setVisible(false);
        expect(!layer.isVisible());

        layer.setVisible(true);
        expect(layer.isVisible());
    };

    "layer has default position mode Absolute"_test = [] {
        MockPluginLayer layer;

        expect(layer.getPositionMode() == PositionMode::Absolute);
    };

    "layer position mode can be set to Relative"_test = [] {
        MockPluginLayer layer;

        layer.setPositionMode(PositionMode::Relative);

        expect(layer.getPositionMode() == PositionMode::Relative);
    };

    "layer has default screen type Main"_test = [] {
        MockPluginLayer layer;

        expect(layer.getScreenType() == ScreenType::Main);
    };

    "layer screen type can be set to Alternate"_test = [] {
        MockPluginLayer layer;

        layer.setScreenType(ScreenType::Alternate);

        expect(layer.getScreenType() == ScreenType::Alternate);
    };

    "layer ID can be set and retrieved"_test = [] {
        MockPluginLayer layer;

        layer.setId(42);

        expect(layer.getId() == 42_u);
    };

    "layer has no focus by default"_test = [] {
        MockPluginLayer layer;

        expect(!layer.hasFocus());
    };

    "layer focus can be set"_test = [] {
        MockPluginLayer layer;

        layer.setFocus(true);
        expect(layer.hasFocus());

        layer.setFocus(false);
        expect(!layer.hasFocus());
    };

    "layer payload can be set"_test = [] {
        MockPluginLayer layer;

        layer.setPayload("test_payload");

        expect(layer.getPayload() == "test_payload");
    };

    "layer needsRender is true by default"_test = [] {
        MockPluginLayer layer;

        expect(layer.needsRender());
    };

    "layer needsRender can be cleared"_test = [] {
        MockPluginLayer layer;

        layer.setNeedsRender(false);

        expect(!layer.needsRender());
    };

    "layer init is called with payload"_test = [] {
        MockPluginLayer layer;

        auto result = layer.init("my_payload");

        expect(result.has_value());
        expect(layer.initCalled());
        expect(layer.getPayload() == "my_payload");
    };

    "layer dispose marks layer as disposed"_test = [] {
        MockPluginLayer layer;
        layer.init("test");

        auto result = layer.dispose();

        expect(result.has_value());
        expect(layer.disposeCalled());
    };

    "layer update increments counter"_test = [] {
        MockPluginLayer layer;

        layer.update(0.016);
        layer.update(0.016);
        layer.update(0.016);

        expect(layer.updateCount() == 3_i);
    };

    "layer update receives delta time"_test = [] {
        MockPluginLayer layer;

        layer.update(0.033);

        expect(layer.lastDeltaTime() == 0.033_d);
    };

    "layer mouse move is tracked"_test = [] {
        MockPluginLayer layer;
        layer.setWantsMouse(true);

        layer.onMouseMove(100.5f, 200.5f);

        expect(layer.lastMouseX() == 100.5_f);
        expect(layer.lastMouseY() == 200.5_f);
        expect(layer.mouseMoveCount() == 1_i);
    };

    "layer mouse button is tracked"_test = [] {
        MockPluginLayer layer;
        layer.setWantsMouse(true);

        layer.onMouseButton(0, true);
        layer.onMouseButton(0, false);

        expect(layer.mouseButtonCount() == 2_i);
    };

    "layer wantsKeyboard returns configured value"_test = [] {
        MockPluginLayer layer;

        expect(!layer.wantsKeyboard());

        layer.setWantsKeyboard(true);
        expect(layer.wantsKeyboard());
    };

    "layer wantsMouse returns configured value"_test = [] {
        MockPluginLayer layer;

        expect(!layer.wantsMouse());

        layer.setWantsMouse(true);
        expect(layer.wantsMouse());
    };

    "layer onResize updates pixel dimensions"_test = [] {
        MockPluginLayer layer;

        layer.onResize(1024, 768);

        expect(layer.getPixelWidth() == 1024_u);
        expect(layer.getPixelHeight() == 768_u);
    };

    "relative layer position updated on scroll simulation"_test = [] {
        MockPluginLayer layer;
        layer.setPositionMode(PositionMode::Relative);
        layer.setPosition(5, 10);

        // Simulate scroll: content moves up by 3 rows
        // Layer Y should decrease by 3
        int scrollDelta = 3;
        layer.setPosition(layer.getX(), layer.getY() - scrollDelta);

        expect(layer.getY() == 7_i);
    };

    "absolute layer position not affected by scroll simulation"_test = [] {
        MockPluginLayer layer;
        layer.setPositionMode(PositionMode::Absolute);
        layer.setPosition(5, 10);

        // For absolute layers, position should not change
        // (scroll handling is done by PluginManager, not layer)
        expect(layer.getY() == 10_i);
    };
};
