/*
 * yconfig contract test — headless, no GPU.
 *
 * Exercises the YAML loader's sequence handling: flat scalar lists
 * (shaders/preload/glyphs style) and structured list entries (mappings
 * inside a sequence, the terminal font range table), plus slash-path
 * navigation into indexed entries.
 */

#include <yetty/yconfig/config.h>

#include "ytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TEST_YAML_PATH = "yconfig-structured-test.yaml";

static void write_test_yaml(struct ytest *test)
{
    static const char yaml_text[] = "terminal:\n"
                                    "  text-layer:\n"
                                    "    font:\n"
                                    "      render-method: msdf\n"
                                    "      ranges:\n"
                                    "        - from: 0x4E00\n"
                                    "          to: 0x9FFF\n"
                                    "          font: NotoSansMonoCJKsc\n"
                                    "          render-method: raster\n"
                                    "        - from: 0x1F300\n"
                                    "          to: 0x1FAFF\n"
                                    "          font: NotoColorEmoji\n"
                                    "          render-method: raster\n"
                                    "        - from: 128\n"
                                    "          to: 255\n"
                                    "          font: DejaVuSansMNerdFontMono\n"
                                    "          render-method: msdf\n"
                                    "shaders:\n"
                                    "  preload:\n"
                                    "    glyphs:\n"
                                    "      - spinner\n"
                                    "      - hourglass\n";

    FILE *yaml_file = fopen(TEST_YAML_PATH, "w");
    YTEST_REQUIRE(test, yaml_file != NULL);
    fwrite(yaml_text, 1, sizeof(yaml_text) - 1, yaml_file);
    fclose(yaml_file);
}

static void test_structured_list_entries(struct ytest *test)
{
    write_test_yaml(test);

    char *argv[] = {"yconfig-test", "-c", (char *)TEST_YAML_PATH, NULL};
    struct yetty_yconfig_result config_res = yetty_yconfig_create(3, argv);
    YTEST_REQUIRE_OK(test, config_res);
    struct yetty_yconfig_config *config = config_res.value;

    /* The range list is countable. */
    YTEST_CHECK_EQ_SIZE(
        test, config->ops->get_array_count(config, YETTY_YCONFIG_KEY_TERMINAL_FONT_RANGES), 3);

    /* Structured fields resolve as <list>/<index>/<field>. */
    YTEST_REQUIRE_STR_EQ(
        test, config->ops->get_string(config, "terminal/text-layer/font/ranges/0/from", "missing"),
        "0x4E00");
    YTEST_REQUIRE_STR_EQ(
        test, config->ops->get_string(config, "terminal/text-layer/font/ranges/0/font", "missing"),
        "NotoSansMonoCJKsc");
    YTEST_REQUIRE_STR_EQ(test,
                         config->ops->get_string(
                             config, "terminal/text-layer/font/ranges/1/render-method", "missing"),
                         "raster");
    YTEST_REQUIRE_STR_EQ(
        test, config->ops->get_string(config, "terminal/text-layer/font/ranges/2/to", "missing"),
        "255");
    YTEST_CHECK_EQ_SIZE(
        test, config->ops->get_int(config, "terminal/text-layer/font/ranges/2/from", -1), 128);

    /* Scalar next to the list is untouched. */
    YTEST_REQUIRE_STR_EQ(
        test,
        config->ops->get_string(config, YETTY_YCONFIG_KEY_TERMINAL_FONT_RENDER_METHOD, "missing"),
        "msdf");

    /* Flat scalar lists keep working. */
    YTEST_CHECK_EQ_SIZE(test, config->ops->get_array_count(config, "shaders/preload/glyphs"), 2);
    YTEST_REQUIRE_STR_EQ(
        test, config->ops->get_array_item(config, "shaders/preload/glyphs", 1, "missing"),
        "hourglass");

    config->ops->destroy(config);
    remove(TEST_YAML_PATH);
}

int main(void)
{
    struct ytest test = ytest_begin("yconfig_config");
    YTEST_RUN(&test, test_structured_list_entries);
    return ytest_end(&test);
}
