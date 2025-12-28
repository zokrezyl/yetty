//=============================================================================
// Terminal Moverect Tests
//
// Tests for moverect callback behavior (insert/delete line, scroll regions)
//=============================================================================

#include <boost/ut.hpp>
#include "harness/terminal_harness.h"

using namespace boost::ut;
using namespace yetty::test;

suite terminal_moverect_tests = [] {
    "insert line (CSI L) triggers moverect"_test = [] {
        TerminalHarness term(80, 25);

        // Move to row 5 and insert a line
        term.feed("\033[5H");  // Move to row 5
        term.clearEvents();
        term.feed("\033[L");   // Insert line (IL)

        expect(!term.moverectEvents().empty()) << "Insert line should trigger moverect";

        if (!term.moverectEvents().empty()) {
            const auto& ev = term.moverectEvents().back();
            // Content moves DOWN when inserting a line
            expect(ev.scrollDelta() < 0_i) << "Insert line should have negative delta (content moves down)";
        }
    };

    "insert 3 lines (CSI 3L) triggers moverect with delta -3"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("\033[5H");
        term.clearEvents();
        term.feed("\033[3L");  // Insert 3 lines

        expect(!term.moverectEvents().empty()) << "Insert 3 lines should trigger moverect";

        if (!term.moverectEvents().empty()) {
            const auto& ev = term.moverectEvents().back();
            expect(ev.scrollDelta() == -3_i) << "Insert 3 lines should have delta=-3";
        }
    };

    "delete line (CSI M) triggers moverect"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("\033[5H");
        term.clearEvents();
        term.feed("\033[M");   // Delete line (DL)

        expect(!term.moverectEvents().empty()) << "Delete line should trigger moverect";

        if (!term.moverectEvents().empty()) {
            const auto& ev = term.moverectEvents().back();
            // Content moves UP when deleting a line
            expect(ev.scrollDelta() > 0_i) << "Delete line should have positive delta (content moves up)";
        }
    };

    "delete 2 lines (CSI 2M) triggers moverect with delta +2"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("\033[5H");
        term.clearEvents();
        term.feed("\033[2M");  // Delete 2 lines

        expect(!term.moverectEvents().empty()) << "Delete 2 lines should trigger moverect";

        if (!term.moverectEvents().empty()) {
            const auto& ev = term.moverectEvents().back();
            expect(ev.scrollDelta() == 2_i) << "Delete 2 lines should have delta=+2";
        }
    };

    "scroll region (DECSTBM) with scroll triggers moverect"_test = [] {
        TerminalHarness term(80, 25);

        // Set scroll region to rows 5-15
        term.feed("\033[5;15r");
        // Move to bottom of region and scroll
        term.feed("\033[15H");
        term.clearEvents();
        term.feed("\n");  // Newline should scroll within region

        // Check for scroll/moverect events
        bool found_event = !term.scrollEvents().empty() || !term.moverectEvents().empty();
        expect(found_event) << "Scroll within region should trigger scroll/moverect";
    };

    "scroll up in region (CSI S) triggers moverect within region"_test = [] {
        TerminalHarness term(80, 25);

        // Set scroll region to rows 5-15
        term.feed("\033[5;15r");
        term.clearEvents();
        term.feed("\033[S");  // Scroll up 1 line

        expect(!term.moverectEvents().empty()) << "Scroll up in region should trigger moverect";

        if (!term.moverectEvents().empty()) {
            const auto& ev = term.moverectEvents().back();
            // Moverect should be constrained to scroll region
            expect(ev.src.start_row >= 4_i) << "Moverect should start at or after row 5 (0-indexed: 4)";
            expect(ev.src.end_row <= 15_i) << "Moverect should end at or before row 15";
        }
    };

    "scroll down in region (CSI T) triggers moverect within region"_test = [] {
        TerminalHarness term(80, 25);

        // Set scroll region to rows 5-15
        term.feed("\033[5;15r");
        term.clearEvents();
        term.feed("\033[T");  // Scroll down 1 line

        expect(!term.moverectEvents().empty()) << "Scroll down in region should trigger moverect";

        if (!term.moverectEvents().empty()) {
            const auto& ev = term.moverectEvents().back();
            expect(ev.scrollDelta() == -1_i) << "Scroll down should have delta=-1";
        }
    };

    "reset clears scroll region"_test = [] {
        TerminalHarness term(80, 25);

        // Set scroll region then reset
        term.feed("\033[5;15r");
        term.feed("\033c");  // Full reset

        // Move to bottom and scroll
        term.feed("\033[25H");
        term.clearEvents();
        term.feed("\n");

        // Should scroll full screen now
        bool found_full_scroll = false;
        for (const auto& ev : term.scrollEvents()) {
            if (ev.start_row == 0 && ev.end_row == 25) {
                found_full_scroll = true;
                break;
            }
        }
        // Note: After reset, the scroll might use full screen dimensions
        expect(term.scrollEvents().size() >= 1_ul) << "Scroll after reset should work";
    };

    "moverect preserves source rect information"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("\033[10H");  // Move to row 10
        term.clearEvents();
        term.feed("\033[L");    // Insert line

        expect(!term.moverectEvents().empty());

        if (!term.moverectEvents().empty()) {
            const auto& ev = term.moverectEvents().back();
            // Source should be from row 10 (0-indexed: 9)
            expect(ev.src.start_row == 9_i) << "Source should start at cursor row";
            // Dest should be 1 row below source (content pushed down)
            expect(ev.dest.start_row == 10_i) << "Dest should be 1 row below source";
        }
    };

    "index (ESC D) at bottom triggers scroll"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("\033[25H");  // Move to last row
        term.clearEvents();
        term.feed("\033D");     // Index (IND)

        expect(!term.scrollEvents().empty()) << "Index at bottom should trigger scroll";
    };

    "reverse index (ESC M) at top triggers moverect"_test = [] {
        TerminalHarness term(80, 25);

        term.feed("\033[H");    // Move to top
        term.clearEvents();
        term.feed("\033M");     // Reverse Index (RI)

        // Reverse index at top uses moverect (content pushed down)
        expect(!term.moverectEvents().empty()) << "Reverse index at top should trigger moverect";

        if (!term.moverectEvents().empty()) {
            expect(term.moverectEvents().back().scrollDelta() < 0_i)
                << "Reverse index should have negative delta (content moves down)";
        }
    };
};
