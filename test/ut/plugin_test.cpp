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

    "plugin is not initialized by default"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        expect(!plugin->isInitialized());
    };

    "plugin init sets initialized flag"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        plugin->init(nullptr);

        expect(plugin->isInitialized());
    };

    "plugin has no layers initially"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        expect(plugin->getLayers().empty());
    };

    "plugin createLayer returns valid layer"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        auto layerResult = plugin->createLayer("test_payload");

        expect(layerResult.has_value());
    };

    "plugin addLayer adds layer to list"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        auto layerResult = plugin->createLayer("payload");
        plugin->addLayer(*layerResult);

        expect(plugin->getLayers().size() == 1_ul);
    };

    "plugin can have multiple layers"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        auto layer1 = *plugin->createLayer("payload1");
        auto layer2 = *plugin->createLayer("payload2");
        auto layer3 = *plugin->createLayer("payload3");

        plugin->addLayer(layer1);
        plugin->addLayer(layer2);
        plugin->addLayer(layer3);

        expect(plugin->getLayers().size() == 3_ul);
    };

    "plugin getLayer returns correct layer by ID"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        auto layer1 = *plugin->createLayer("p1");
        auto layer2 = *plugin->createLayer("p2");
        layer1->setId(100);
        layer2->setId(200);

        plugin->addLayer(layer1);
        plugin->addLayer(layer2);

        auto found = plugin->getLayer(200);
        expect(found != nullptr);
        expect(found->getId() == 200_u);
    };

    "plugin getLayer returns nullptr for unknown ID"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        auto found = plugin->getLayer(999);

        expect(found == nullptr);
    };

    "plugin removeLayer removes layer by ID"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        auto layer = *plugin->createLayer("payload");
        layer->setId(42);
        plugin->addLayer(layer);

        expect(plugin->getLayers().size() == 1_ul);

        auto removeResult = plugin->removeLayer(42);

        expect(removeResult.has_value());
        expect(plugin->getLayers().empty());
    };

    "plugin removeLayer fails for unknown ID"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        auto removeResult = plugin->removeLayer(999);

        expect(!removeResult.has_value());
    };

    "plugin removeLayer calls dispose on layer"_test = [] {
        auto result = MockPlugin::create();
        auto mockPlugin = std::static_pointer_cast<MockPlugin>(*result);

        auto layerResult = mockPlugin->createLayer("payload");
        auto layer = *layerResult;
        layer->setId(42);
        mockPlugin->addLayer(layer);

        mockPlugin->removeLayer(42);

        // Check the created layer was disposed
        auto& createdLayers = mockPlugin->createdLayers();
        expect(!createdLayers.empty());
        expect(createdLayers[0]->disposeCalled());
    };

    "plugin update calls update on all visible layers"_test = [] {
        auto result = MockPlugin::create();
        auto mockPlugin = std::static_pointer_cast<MockPlugin>(*result);

        auto layer1 = std::static_pointer_cast<MockPluginLayer>(*mockPlugin->createLayer("p1"));
        auto layer2 = std::static_pointer_cast<MockPluginLayer>(*mockPlugin->createLayer("p2"));
        mockPlugin->addLayer(layer1);
        mockPlugin->addLayer(layer2);

        mockPlugin->update(0.016);

        expect(layer1->updateCount() == 1_i);
        expect(layer2->updateCount() == 1_i);
    };

    "plugin update skips invisible layers"_test = [] {
        auto result = MockPlugin::create();
        auto mockPlugin = std::static_pointer_cast<MockPlugin>(*result);

        auto layer1 = std::static_pointer_cast<MockPluginLayer>(*mockPlugin->createLayer("p1"));
        auto layer2 = std::static_pointer_cast<MockPluginLayer>(*mockPlugin->createLayer("p2"));
        layer2->setVisible(false);
        mockPlugin->addLayer(layer1);
        mockPlugin->addLayer(layer2);

        mockPlugin->update(0.016);

        expect(layer1->updateCount() == 1_i);
        expect(layer2->updateCount() == 0_i);
    };

    "plugin dispose disposes all layers"_test = [] {
        auto result = MockPlugin::create();
        auto mockPlugin = std::static_pointer_cast<MockPlugin>(*result);

        auto layer1 = std::static_pointer_cast<MockPluginLayer>(*mockPlugin->createLayer("p1"));
        auto layer2 = std::static_pointer_cast<MockPluginLayer>(*mockPlugin->createLayer("p2"));
        mockPlugin->addLayer(layer1);
        mockPlugin->addLayer(layer2);

        mockPlugin->dispose();

        expect(layer1->disposeCalled());
        expect(layer2->disposeCalled());
        expect(mockPlugin->getLayers().empty());
    };

    "plugin onTerminalResize updates layer pixel sizes"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        auto layer = *plugin->createLayer("payload");
        layer->setCellSize(10, 5);  // 10 cols x 5 rows
        plugin->addLayer(layer);

        plugin->onTerminalResize(12, 24);  // cell size 12x24 pixels

        expect(layer->getPixelWidth() == 120_u);  // 10 * 12
        expect(layer->getPixelHeight() == 120_u); // 5 * 24
    };

    "plugin onTerminalResize sets needsRender on layers"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        auto layer = *plugin->createLayer("payload");
        layer->setNeedsRender(false);
        plugin->addLayer(layer);

        plugin->onTerminalResize(12, 24);

        expect(layer->needsRender());
    };

    "layer parent is set when added to plugin"_test = [] {
        auto result = MockPlugin::create();
        auto plugin = *result;

        auto layer = *plugin->createLayer("payload");
        expect(layer->getParent() == nullptr);

        plugin->addLayer(layer);

        expect(layer->getParent() == plugin.get());
    };
};
