//=============================================================================
// Terminal Scroll Tests
//
// Tests for scrollback (sb_pushline/sb_popline) behavior
//=============================================================================

#include <boost/ut.hpp>
#include "harness/terminal_harness.h"

using namespace boost::ut;
using namespace yetty::test;

suite terminal_scroll_tests = [] {
    "linefeed at bottom triggers scroll"_test = [] {
        TerminalHarness term(80, 25);

        // Move to last row and trigger newlines
        term.feed("\033[25H");  // Move to row 25
        expect(term.cursorRow() == 24_i);  // 0-indexed

        term.clearEvents();
        term.feed("\n");  // Should trigger scroll

        expect(term.scrollEvents().size() >= 1_ul) << "Expected scroll event on newline at bottom";
        if (!term.scrollEvents().empty()) {
            expect(term.scrollEvents().back().delta_row == 1_i) << "Content should move UP (delta=1)";
        }
    };

    "linefeed in middle does not scroll"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("\033[10H");  // Move to row 10
        expect(term.cursorRow() == 9_i);

        term.clearEvents();
        term.feed("\n");

        // Should not trigger moverect scroll (might have damage but no scroll)
        bool has_scroll_event = false;
        for (const auto& ev : term.scrollEvents()) {
            if (ev.delta_row != 0) {
                has_scroll_event = true;
                break;
            }
        }
        expect(!has_scroll_event) << "Linefeed in middle should not scroll";
    };

    "multiple newlines at bottom trigger multiple scrolls"_test = [] {
        TerminalHarness term(80, 25);

        // Fill terminal with newlines to reach bottom
        term.feed("\033[25H");  // Move to last row
        term.clearEvents();

        // Send 5 newlines
        term.feed("\n\n\n\n\n");

        // Count scroll events with positive delta
        int scroll_count = 0;
        for (const auto& ev : term.scrollEvents()) {
            if (ev.delta_row > 0) {
                scroll_count += ev.delta_row;
            }
        }
        expect(scroll_count == 5_i) << "Expected 5 scroll events for 5 newlines";
    };

    "CSI S (scroll up) triggers moverect"_test = [] {
        TerminalHarness term(80, 25);
        term.clearEvents();

        term.feed("\033[S");  // Scroll up 1 line

        // CSI S uses moverect, not sb_pushline
        expect(!term.moverectEvents().empty()) << "CSI S should trigger moverect";
        if (!term.moverectEvents().empty()) {
            expect(term.moverectEvents().back().scrollDelta() > 0_i)
                << "CSI S should have positive delta (content moves up)";
        }
    };

    "CSI 2S (scroll up 2) triggers moverect with delta 2"_test = [] {
        TerminalHarness term(80, 25);
        term.clearEvents();

        term.feed("\033[2S");  // Scroll up 2 lines

        expect(!term.moverectEvents().empty()) << "CSI 2S should trigger moverect";
        if (!term.moverectEvents().empty()) {
            int total_delta = 0;
            for (const auto& ev : term.moverectEvents()) {
                total_delta += ev.scrollDelta();
            }
            expect(total_delta == 2_i) << "CSI 2S should scroll up by 2";
        }
    };

    "CSI T (scroll down) triggers moverect"_test = [] {
        TerminalHarness term(80, 25);
        term.clearEvents();

        term.feed("\033[T");  // Scroll down 1 line

        expect(!term.moverectEvents().empty()) << "CSI T should trigger moverect";
        if (!term.moverectEvents().empty()) {
            expect(term.moverectEvents().back().scrollDelta() < 0_i)
                << "CSI T should have negative delta (content moves down)";
        }
    };

    "CSI 3T (scroll down 3) triggers moverect with delta -3"_test = [] {
        TerminalHarness term(80, 25);
        term.clearEvents();

        term.feed("\033[3T");  // Scroll down 3 lines

        expect(!term.moverectEvents().empty()) << "CSI 3T should trigger moverect";
        if (!term.moverectEvents().empty()) {
            int total_delta = 0;
            for (const auto& ev : term.moverectEvents()) {
                total_delta += ev.scrollDelta();
            }
            expect(total_delta == -3_i) << "CSI 3T should scroll down by 3";
        }
    };
};
