#pragma once

#include <cstdint>

namespace yetty {

/**
 * Emoji codepoint detection utilities.
 *
 * Reference: https://unicode.org/emoji/charts/full-emoji-list.html
 */

// Check if a codepoint is an emoji
inline bool isEmoji(uint32_t codepoint) {
    // Miscellaneous Symbols (subset that are commonly emoji)
    if (codepoint >= 0x2600 && codepoint <= 0x26FF) return true;

    // Dingbats (subset)
    if (codepoint >= 0x2700 && codepoint <= 0x27BF) return true;

    // Emoticons
    if (codepoint >= 0x1F600 && codepoint <= 0x1F64F) return true;

    // Miscellaneous Symbols and Pictographs
    if (codepoint >= 0x1F300 && codepoint <= 0x1F5FF) return true;

    // Transport and Map Symbols
    if (codepoint >= 0x1F680 && codepoint <= 0x1F6FF) return true;

    // Supplemental Symbols and Pictographs
    if (codepoint >= 0x1F900 && codepoint <= 0x1F9FF) return true;

    // Symbols and Pictographs Extended-A
    if (codepoint >= 0x1FA00 && codepoint <= 0x1FA6F) return true;

    // Symbols and Pictographs Extended-B
    if (codepoint >= 0x1FA70 && codepoint <= 0x1FAFF) return true;

    // Regional Indicator Symbols (flags)
    if (codepoint >= 0x1F1E0 && codepoint <= 0x1F1FF) return true;

    // Mahjong Tiles
    if (codepoint >= 0x1F000 && codepoint <= 0x1F02F) return true;

    // Playing Cards
    if (codepoint >= 0x1F0A0 && codepoint <= 0x1F0FF) return true;

    // Enclosed Alphanumeric Supplement (some are emoji)
    if (codepoint >= 0x1F100 && codepoint <= 0x1F1FF) return true;

    // Arrows (some are emoji in certain contexts)
    if (codepoint >= 0x2190 && codepoint <= 0x21FF) return true;

    // Mathematical Operators (subset)
    if (codepoint == 0x2714 || codepoint == 0x2716) return true; // Check marks

    // Specific commonly-used emoji codepoints
    switch (codepoint) {
        // Hearts
        case 0x2764: // Red Heart
        case 0x2665: // Black Heart Suit
        case 0x2661: // White Heart Suit
        // Stars
        case 0x2B50: // Star
        case 0x2B55: // Heavy Large Circle
        // Arrows
        case 0x27A1: // Black Rightwards Arrow
        // Numbers/keycaps (when followed by variation selector)
        case 0x0023: // #
        case 0x002A: // *
        case 0x0030: case 0x0031: case 0x0032: case 0x0033: case 0x0034: // 0-4
        case 0x0035: case 0x0036: case 0x0037: case 0x0038: case 0x0039: // 5-9
            return false; // These need VS16 to be emoji, default to text
        default:
            break;
    }

    return false;
}

// Check if codepoint is a Variation Selector 16 (emoji presentation)
inline bool isVariationSelector16(uint32_t codepoint) {
    return codepoint == 0xFE0F;
}

// Check if codepoint is a Variation Selector 15 (text presentation)
inline bool isVariationSelector15(uint32_t codepoint) {
    return codepoint == 0xFE0E;
}

// Check if codepoint is a Zero Width Joiner (for emoji sequences)
inline bool isZWJ(uint32_t codepoint) {
    return codepoint == 0x200D;
}

// Check if codepoint is a skin tone modifier
inline bool isSkinToneModifier(uint32_t codepoint) {
    return codepoint >= 0x1F3FB && codepoint <= 0x1F3FF;
}

// Common emoji list for preloading (top ~200 most used)
// These are indexed by their position in this array for the emoji atlas
constexpr uint32_t COMMON_EMOJIS[] = {
    // Smileys & Emotion (most used)
    0x1F600, // 😀 grinning face
    0x1F601, // 😁 beaming face
    0x1F602, // 😂 face with tears of joy
    0x1F603, // 😃 grinning face with big eyes
    0x1F604, // 😄 grinning face with smiling eyes
    0x1F605, // 😅 grinning face with sweat
    0x1F606, // 😆 grinning squinting face
    0x1F607, // 😇 smiling face with halo
    0x1F608, // 😈 smiling face with horns
    0x1F609, // 😉 winking face
    0x1F60A, // 😊 smiling face with smiling eyes
    0x1F60B, // 😋 face savoring food
    0x1F60C, // 😌 relieved face
    0x1F60D, // 😍 smiling face with heart-eyes
    0x1F60E, // 😎 smiling face with sunglasses
    0x1F60F, // 😏 smirking face
    0x1F610, // 😐 neutral face
    0x1F611, // 😑 expressionless face
    0x1F612, // 😒 unamused face
    0x1F613, // 😓 downcast face with sweat
    0x1F614, // 😔 pensive face
    0x1F615, // 😕 confused face
    0x1F616, // 😖 confounded face
    0x1F617, // 😗 kissing face
    0x1F618, // 😘 face blowing a kiss
    0x1F619, // 😙 kissing face with smiling eyes
    0x1F61A, // 😚 kissing face with closed eyes
    0x1F61B, // 😛 face with tongue
    0x1F61C, // 😜 winking face with tongue
    0x1F61D, // 😝 squinting face with tongue
    0x1F61E, // 😞 disappointed face
    0x1F61F, // 😟 worried face
    0x1F620, // 😠 angry face
    0x1F621, // 😡 pouting face
    0x1F622, // 😢 crying face
    0x1F623, // 😣 persevering face
    0x1F624, // 😤 face with steam from nose
    0x1F625, // 😥 sad but relieved face
    0x1F626, // 😦 frowning face with open mouth
    0x1F627, // 😧 anguished face
    0x1F628, // 😨 fearful face
    0x1F629, // 😩 weary face
    0x1F62A, // 😪 sleepy face
    0x1F62B, // 😫 tired face
    0x1F62C, // 😬 grimacing face
    0x1F62D, // 😭 loudly crying face
    0x1F62E, // 😮 face with open mouth
    0x1F62F, // 😯 hushed face
    0x1F630, // 😰 anxious face with sweat
    0x1F631, // 😱 face screaming in fear
    0x1F632, // 😲 astonished face
    0x1F633, // 😳 flushed face
    0x1F634, // 😴 sleeping face
    0x1F635, // 😵 dizzy face
    0x1F636, // 😶 face without mouth
    0x1F637, // 😷 face with medical mask

    // Hearts
    0x2764,  // ❤️ red heart
    0x1F493, // 💓 beating heart
    0x1F494, // 💔 broken heart
    0x1F495, // 💕 two hearts
    0x1F496, // 💖 sparkling heart
    0x1F497, // 💗 growing heart
    0x1F498, // 💘 heart with arrow
    0x1F499, // 💙 blue heart
    0x1F49A, // 💚 green heart
    0x1F49B, // 💛 yellow heart
    0x1F49C, // 💜 purple heart
    0x1F49D, // 💝 heart with ribbon
    0x1F49E, // 💞 revolving hearts
    0x1F49F, // 💟 heart decoration
    0x1F5A4, // 🖤 black heart
    0x1F90D, // 🤍 white heart
    0x1F90E, // 🤎 brown heart
    0x1F9E1, // 🧡 orange heart

    // Gestures
    0x1F44D, // 👍 thumbs up
    0x1F44E, // 👎 thumbs down
    0x1F44A, // 👊 oncoming fist
    0x1F44B, // 👋 waving hand
    0x1F44C, // 👌 OK hand
    0x1F44F, // 👏 clapping hands
    0x1F64F, // 🙏 folded hands
    0x1F4AA, // 💪 flexed biceps
    0x270B,  // ✋ raised hand
    0x270C,  // ✌️ victory hand
    0x1F91E, // 🤞 crossed fingers
    0x1F91F, // 🤟 love-you gesture
    0x1F918, // 🤘 sign of the horns
    0x1F919, // 🤙 call me hand

    // Objects & Symbols
    0x2B50,  // ⭐ star
    0x1F31F, // 🌟 glowing star
    0x1F4A5, // 💥 collision
    0x1F4A6, // 💦 sweat droplets
    0x1F4A8, // 💨 dashing away
    0x1F4AB, // 💫 dizzy
    0x1F4AC, // 💬 speech balloon
    0x1F4AD, // 💭 thought balloon
    0x1F4AF, // 💯 hundred points
    0x1F525, // 🔥 fire
    0x26A1,  // ⚡ high voltage
    0x2728,  // ✨ sparkles

    // Animals
    0x1F436, // 🐶 dog face
    0x1F431, // 🐱 cat face
    0x1F42D, // 🐭 mouse face
    0x1F439, // 🐹 hamster
    0x1F430, // 🐰 rabbit face
    0x1F98A, // 🦊 fox
    0x1F43B, // 🐻 bear
    0x1F43C, // 🐼 panda
    0x1F428, // 🐨 koala
    0x1F42F, // 🐯 tiger face
    0x1F981, // 🦁 lion
    0x1F42E, // 🐮 cow face
    0x1F437, // 🐷 pig face
    0x1F438, // 🐸 frog
    0x1F412, // 🐒 monkey
    0x1F414, // 🐔 chicken
    0x1F427, // 🐧 penguin
    0x1F426, // 🐦 bird
    0x1F40D, // 🐍 snake
    0x1F422, // 🐢 turtle
    0x1F41D, // 🐝 honeybee
    0x1F41B, // 🐛 bug
    0x1F98B, // 🦋 butterfly

    // Food & Drink
    0x1F34E, // 🍎 red apple
    0x1F34F, // 🍏 green apple
    0x1F350, // 🍐 pear
    0x1F34A, // 🍊 tangerine
    0x1F34B, // 🍋 lemon
    0x1F34C, // 🍌 banana
    0x1F349, // 🍉 watermelon
    0x1F347, // 🍇 grapes
    0x1F353, // 🍓 strawberry
    0x1F352, // 🍒 cherries
    0x1F351, // 🍑 peach
    0x1F355, // 🍕 pizza
    0x1F354, // 🍔 hamburger
    0x1F35F, // 🍟 french fries
    0x1F32D, // 🌭 hot dog
    0x1F32E, // 🌮 taco
    0x1F37A, // 🍺 beer mug
    0x1F377, // 🍷 wine glass
    0x2615,  // ☕ hot beverage

    // Weather & Nature
    0x2600,  // ☀️ sun
    0x1F31E, // 🌞 sun with face
    0x1F31D, // 🌝 full moon face
    0x1F31A, // 🌚 new moon face
    0x1F319, // 🌙 crescent moon
    // 0x2B50 removed - duplicate of line 199
    0x1F308, // 🌈 rainbow
    0x2601,  // ☁️ cloud
    0x26C5,  // ⛅ sun behind cloud
    0x1F327, // 🌧️ cloud with rain
    // 0x26A1 removed - duplicate of line 209
    0x1F4A7, // 💧 droplet

    // Checkmarks & Status
    0x2705,  // ✅ check mark button
    0x274C,  // ❌ cross mark
    0x2714,  // ✔️ check mark
    0x2716,  // ✖️ multiplication sign
    0x1F6AB, // 🚫 prohibited
    0x26A0,  // ⚠️ warning
    0x2139,  // ℹ️ information
    0x1F4A1, // 💡 light bulb
    0x1F512, // 🔒 locked
    0x1F513, // 🔓 unlocked

    // Arrows
    0x2B06,  // ⬆️ up arrow
    0x2B07,  // ⬇️ down arrow
    0x27A1,  // ➡️ right arrow
    0x2B05,  // ⬅️ left arrow
    0x2194,  // ↔️ left right arrow
    0x2195,  // ↕️ up down arrow

    // Tech
    0x1F4BB, // 💻 laptop
    0x1F4F1, // 📱 mobile phone
    0x2328,  // ⌨️ keyboard
    0x1F5A5, // 🖥️ desktop computer
    0x1F3AE, // 🎮 video game
    0x1F680, // 🚀 rocket

    // Misc
    0x1F389, // 🎉 party popper
    0x1F38A, // 🎊 confetti ball
    0x1F381, // 🎁 wrapped gift
    0x1F4E7, // 📧 e-mail
    0x1F4DD, // 📝 memo
    0x1F4DA, // 📚 books
    0x1F4D6, // 📖 open book
    0x1F4CC, // 📌 pushpin
    0x1F4CE, // 📎 paperclip
    0x1F4C1, // 📁 file folder
    0x1F4C2, // 📂 open file folder
};

constexpr size_t COMMON_EMOJI_COUNT = sizeof(COMMON_EMOJIS) / sizeof(COMMON_EMOJIS[0]);

// Find emoji index in common list (returns -1 if not found)
inline int findCommonEmojiIndex(uint32_t codepoint) {
    for (size_t i = 0; i < COMMON_EMOJI_COUNT; ++i) {
        if (COMMON_EMOJIS[i] == codepoint) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace yetty
