//=============================================================================
// Plugin Tests
//
// Tests for Plugin base class functionality
//=============================================================================

#include <boost/ut.hpp>
#include "harness/mock_plugin.h"

using namespace boost::ut;
using namespace yetty;
using namespace yetty::test;

suite plugin_tests = [] {
    "plugin can be created"_test = [] {
        auto result = MockPlugin::create();

        expect(result.has_value());
    };

    "plugin has correct name"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        expect(std::string(plugin->pluginName()) == "mock");
    };

    "plugin is initialized after create"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        expect(plugin->isInitialized());
    };

    "plugin createWidget returns valid widget"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        auto widgetResult = plugin->createWidget("", nullptr, nullptr, nullptr, 0, 0, 10, 10, "", "test_payload");

        expect(widgetResult.has_value());
    };

    "created widget has correct payload"_test = [] {
        auto result = MockPlugin::create();
        auto mockPlugin = std::static_pointer_cast<MockPlugin>(*result);

        auto widgetResult = mockPlugin->createWidget("", nullptr, nullptr, nullptr, 0, 0, 10, 10, "", "test_payload");

        expect(widgetResult.has_value());
        auto& createdWidgets = mockPlugin->createdWidgets();
        expect(createdWidgets.size() == 1_ul);
    };

    "plugin can create multiple widgets"_test = [] {
        auto result = MockPlugin::create();
        auto mockPlugin = std::static_pointer_cast<MockPlugin>(*result);

        mockPlugin->createWidget("", nullptr, nullptr, nullptr, 0, 0, 10, 10, "", "payload1");
        mockPlugin->createWidget("", nullptr, nullptr, nullptr, 0, 0, 10, 10, "", "payload2");
        mockPlugin->createWidget("", nullptr, nullptr, nullptr, 0, 0, 10, 10, "", "payload3");

        auto& createdWidgets = mockPlugin->createdWidgets();
        expect(createdWidgets.size() == 3_ul);
    };

    "widget dispose can be called"_test = [] {
        auto result = MockPlugin::create();
        auto mockPlugin = std::static_pointer_cast<MockPlugin>(*result);

        auto widgetResult = mockPlugin->createWidget("", nullptr, nullptr, nullptr, 0, 0, 10, 10, "", "payload");
        auto widget = *widgetResult;

        auto disposeResult = widget->dispose();

        expect(disposeResult.has_value());
        auto& createdWidgets = mockPlugin->createdWidgets();
        expect(createdWidgets[0]->disposeCalled());
    };

    "plugin dispose succeeds"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        auto disposeResult = plugin->dispose();

        expect(disposeResult.has_value());
    };
};
