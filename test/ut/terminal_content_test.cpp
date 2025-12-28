//=============================================================================
// Terminal Content Tests
//
// Tests for terminal content manipulation and cell access
// Covers: reset(), getChar(), scrollback restore
//=============================================================================

#include <boost/ut.hpp>
#include "harness/terminal_harness.h"

using namespace boost::ut;
using namespace yetty::test;

suite terminal_content_tests = [] {
    "getChar returns correct character"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Hello");

        expect(term.getChar(0, 0) == uint32_t('H'));
        expect(term.getChar(0, 1) == uint32_t('e'));
        expect(term.getChar(0, 2) == uint32_t('l'));
        expect(term.getChar(0, 3) == uint32_t('l'));
        expect(term.getChar(0, 4) == uint32_t('o'));
    };

    "getChar returns 0 for empty cells"_test = [] {
        TerminalHarness term(80, 25);

        // Empty terminal should have null/space characters
        uint32_t ch = term.getChar(5, 5);
        expect((ch == 0 || ch == ' ') >> fatal);
    };

    "getChar works after cursor movement"_test = [] {
        TerminalHarness term(80, 25);

        // Move to row 5, col 10 and write
        term.feed("\033[5;10H");  // Move cursor
        term.feed("TEST");

        expect(term.getChar(4, 9) == uint32_t('T'));   // 0-indexed
        expect(term.getChar(4, 10) == uint32_t('E'));
        expect(term.getChar(4, 11) == uint32_t('S'));
        expect(term.getChar(4, 12) == uint32_t('T'));
    };

    "getChar returns unicode codepoints"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("日本語");  // Japanese characters

        // Each character should be retrievable
        expect(term.getChar(0, 0) != 0_u);
    };

    "reset clears terminal content"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Hello World");
        expect(term.getChar(0, 0) == uint32_t('H'));

        term.reset();

        // After reset, content should be cleared
        uint32_t ch = term.getChar(0, 0);
        expect((ch == 0 || ch == ' ') >> fatal) << "Reset should clear content";
    };

    "reset clears cursor position"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("\033[10;20H");  // Move cursor
        expect(term.cursorRow() == 9_i);
        expect(term.cursorCol() == 19_i);

        term.reset();

        expect(term.cursorRow() == 0_i) << "Reset should move cursor to row 0";
        expect(term.cursorCol() == 0_i) << "Reset should move cursor to col 0";
    };

    "reset clears events"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("\033[25H\n");  // Generate scroll event
        expect(!term.scrollEvents().empty());

        term.reset();

        expect(term.scrollEvents().empty()) << "Reset should clear scroll events";
        expect(term.moverectEvents().empty()) << "Reset should clear moverect events";
        expect(term.damageEvents().empty()) << "Reset should clear damage events";
    };

    "content persists across multiple feeds"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Hello");
        term.feed(" ");
        term.feed("World");

        expect(term.getChar(0, 0) == uint32_t('H'));
        expect(term.getChar(0, 5) == uint32_t(' '));
        expect(term.getChar(0, 6) == uint32_t('W'));
    };

    "overwrite replaces content"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("AAAA");
        term.feed("\033[1;1H");  // Move to start
        term.feed("BB");

        expect(term.getChar(0, 0) == uint32_t('B'));
        expect(term.getChar(0, 1) == uint32_t('B'));
        expect(term.getChar(0, 2) == uint32_t('A'));
        expect(term.getChar(0, 3) == uint32_t('A'));
    };

    "erase clears content"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Hello World");
        term.feed("\033[1;1H");   // Move to start
        term.feed("\033[2K");     // Erase entire line

        uint32_t ch = term.getChar(0, 0);
        expect((ch == 0 || ch == ' ') >> fatal) << "Erase should clear content";
    };

    "clear screen erases all content"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Line 1\n");
        term.feed("Line 2\n");
        term.feed("Line 3");

        term.feed("\033[2J");  // Clear entire screen

        // All content should be cleared
        uint32_t ch1 = term.getChar(0, 0);
        uint32_t ch2 = term.getChar(1, 0);
        expect((ch1 == 0 || ch1 == ' ') >> fatal);
        expect((ch2 == 0 || ch2 == ' ') >> fatal);
    };
};
