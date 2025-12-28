//=============================================================================
// Terminal Selection Tests
//
// Tests for text selection functionality
// Covers: startSelection, extendSelection, clearSelection, getSelectedText
//=============================================================================

#include <boost/ut.hpp>
#include "harness/terminal_harness.h"

using namespace boost::ut;
using namespace yetty::test;

suite terminal_selection_tests = [] {
    "no selection by default"_test = [] {
        TerminalHarness term(80, 25);

        expect(!term.hasSelection());
    };

    "startSelection sets selection active"_test = [] {
        TerminalHarness term(80, 25);

        term.startSelection(0, 0);

        expect(term.hasSelection());
    };

    "clearSelection removes selection"_test = [] {
        TerminalHarness term(80, 25);

        term.startSelection(0, 0);
        expect(term.hasSelection());

        term.clearSelection();
        expect(!term.hasSelection());
    };

    "isInSelection returns true for cells in selection"_test = [] {
        TerminalHarness term(80, 25);

        term.startSelection(0, 5);
        term.extendSelection(0, 10);

        expect(term.isInSelection(0, 5));
        expect(term.isInSelection(0, 7));
        expect(term.isInSelection(0, 10));
    };

    "isInSelection returns false for cells outside selection"_test = [] {
        TerminalHarness term(80, 25);

        term.startSelection(0, 5);
        term.extendSelection(0, 10);

        expect(!term.isInSelection(0, 4));
        expect(!term.isInSelection(0, 11));
        expect(!term.isInSelection(1, 7));  // Different row
    };

    "selection works when extended backwards"_test = [] {
        TerminalHarness term(80, 25);

        term.startSelection(0, 10);
        term.extendSelection(0, 5);  // Extend backwards

        expect(term.isInSelection(0, 5));
        expect(term.isInSelection(0, 7));
        expect(term.isInSelection(0, 10));
    };

    "multi-line selection works"_test = [] {
        TerminalHarness term(80, 25);

        term.startSelection(0, 5);
        term.extendSelection(2, 10);

        // First row from col 5
        expect(term.isInSelection(0, 5));
        expect(term.isInSelection(0, 79));
        expect(!term.isInSelection(0, 4));

        // Middle row - entire row
        expect(term.isInSelection(1, 0));
        expect(term.isInSelection(1, 40));

        // Last row up to col 10
        expect(term.isInSelection(2, 0));
        expect(term.isInSelection(2, 10));
        expect(!term.isInSelection(2, 11));
    };

    "getSelectedText extracts text from single line"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Hello World");
        term.startSelection(0, 0);
        term.extendSelection(0, 5);

        std::string text = term.getSelectedText();
        expect(text == "Hello") << "Got: '" << text << "'";
    };

    "getSelectedText extracts partial word"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Hello World");
        term.startSelection(0, 6);
        term.extendSelection(0, 11);

        std::string text = term.getSelectedText();
        expect(text == "World") << "Got: '" << text << "'";
    };

    "getSelectedText extracts multi-line text"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Line One\r\nLine Two");
        term.startSelection(0, 0);
        term.extendSelection(1, 8);

        std::string text = term.getSelectedText();
        // Should contain both lines
        expect(text.find("Line") != std::string::npos);
    };

    "word selection mode selects whole word"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Hello World Test");
        // Click in middle of "World"
        term.startSelection(0, 8, TerminalHarness::SelectionMode::Word);

        std::string text = term.getSelectedText();
        expect(text == "World") << "Got: '" << text << "'";
    };

    "line selection mode selects full line"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Hello World");
        term.startSelection(0, 5, TerminalHarness::SelectionMode::Line);

        // Start should be at column 0
        expect(term.isInSelection(0, 0));
        // End should be at end of terminal
        expect(term.isInSelection(0, 79));
    };

    "extendSelection in line mode extends full lines"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Line One\r\nLine Two\r\nLine Three");
        term.startSelection(0, 5, TerminalHarness::SelectionMode::Line);
        term.extendSelection(2, 3);  // Extend to third line

        // All three lines should be fully selected
        expect(term.isInSelection(0, 0));
        expect(term.isInSelection(0, 79));
        expect(term.isInSelection(1, 0));
        expect(term.isInSelection(1, 79));
        expect(term.isInSelection(2, 0));
        expect(term.isInSelection(2, 79));
    };

    "selection returns empty string when no selection"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Hello World");

        std::string text = term.getSelectedText();
        expect(text.empty());
    };

    "selection handles empty cells"_test = [] {
        TerminalHarness term(80, 25);

        // Select in area with no content
        term.startSelection(5, 10);
        term.extendSelection(5, 20);

        // Should not crash, may return spaces or empty
        std::string text = term.getSelectedText();
        // Just verify it doesn't crash
        expect(true);
    };

    "selection handles unicode text"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("Hello 日本語 World");
        term.startSelection(0, 0);
        term.extendSelection(0, 15);

        std::string text = term.getSelectedText();
        expect(!text.empty());
        expect(text.find("Hello") != std::string::npos);
    };
};
