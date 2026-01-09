//=============================================================================
// SharedGrid Unit Tests
//
// Tests for shared memory grid with double-buffering
// Covers: creation, buffer swapping, data access, codepoint storage
//=============================================================================

#include <boost/ut.hpp>
#include "yetty/shared-grid.h"
#include <cstring>
#include <thread>
#include <chrono>

using namespace boost::ut;
using namespace yetty;

suite shared_grid_tests = [] {
    //=========================================================================
    // Basic creation tests
    //=========================================================================
    
    "SharedGrid creates with correct dimensions"_test = [] {
        const char* shmName = "/test-shm-create";
        
        // Cleanup any leftover
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 80, 24);
        expect(grid != nullptr) << "Should create SharedGrid";
        expect(grid->getCols() == 80_u);
        expect(grid->getRows() == 24_u);
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    "SharedGrid header has correct magic and version"_test = [] {
        const char* shmName = "/test-shm-magic";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 80, 24);
        expect(grid != nullptr);
        
        // Access raw header
        const auto* hdr = grid->header();
        expect(hdr != nullptr);
        expect(hdr->magic == 0x59455454u) << "Magic should be 'YETT'";
        expect(hdr->version == 3_u) << "Version should be 3";
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    "SharedGrid client can open server-created shm"_test = [] {
        const char* shmName = "/test-shm-client";
        SharedGrid::unlink(shmName);
        
        // Server creates
        auto* server = SharedGrid::createServer(shmName, 100, 30);
        expect(server != nullptr);
        
        // Client opens
        auto* client = SharedGrid::openClient(shmName);
        expect(client != nullptr) << "Client should open shared memory";
        expect(client->getCols() == 100_u);
        expect(client->getRows() == 30_u);
        
        delete client;
        delete server;
        SharedGrid::unlink(shmName);
    };
    
    //=========================================================================
    // Double-buffering tests
    //=========================================================================
    
    "SharedGrid starts with buffer 0 active"_test = [] {
        const char* shmName = "/test-shm-buffer0";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 80, 24);
        const auto* hdr = grid->header();
        
        expect(hdr->activeBuffer.load() == 0_u) << "Should start with buffer 0";
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    "SharedGrid swapBuffers toggles active buffer"_test = [] {
        const char* shmName = "/test-shm-swap";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 80, 24);
        const auto* hdr = grid->header();
        
        expect(hdr->activeBuffer.load() == 0_u);
        
        grid->swapBuffers();
        expect(hdr->activeBuffer.load() == 1_u) << "Should be buffer 1 after swap";
        
        grid->swapBuffers();
        expect(hdr->activeBuffer.load() == 0_u) << "Should be buffer 0 after second swap";
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    "SharedGrid sequence number increments on copyFromGrid"_test = [] {
        const char* shmName = "/test-shm-seq";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 80, 24);
        Grid srcGrid(80, 24);
        
        // Initial state: buffer 0 is active, seq=0
        uint32_t seq0 = grid->activeBufferHeader()->sequenceNumber;
        expect(seq0 == 0_u) << "Initial sequence should be 0";
        
        // copyFromGrid writes to back buffer (buffer 1) and increments its sequence
        grid->copyFromGrid(srcGrid);
        // Now back buffer (buffer 1) has seq=1, active buffer (buffer 0) still has seq=0
        
        // Swap - now buffer 1 is active with seq=1
        grid->swapBuffers();
        uint32_t seq1 = grid->activeBufferHeader()->sequenceNumber;
        expect(seq1 == 1_u) << "Sequence should be 1 after first copy+swap";
        
        // Copy again - writes to back buffer (buffer 0), increments to seq=1
        grid->copyFromGrid(srcGrid);
        grid->swapBuffers();  // Now buffer 0 is active with seq=1
        uint32_t seq2 = grid->activeBufferHeader()->sequenceNumber;
        expect(seq2 == 1_u) << "Buffer 0 should have seq 1 after its first copy";
        
        // Copy again - writes to back buffer (buffer 1), increments to seq=2
        grid->copyFromGrid(srcGrid);
        grid->swapBuffers();  // Now buffer 1 is active with seq=2
        uint32_t seq3 = grid->activeBufferHeader()->sequenceNumber;
        expect(seq3 == 2_u) << "Buffer 1 should have seq 2 after second copy";
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    //=========================================================================
    // Data storage tests
    //=========================================================================
    
    "SharedGrid stores codepoints as uint32"_test = [] {
        const char* shmName = "/test-shm-codepoint";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 10, 5);
        
        // Create a simple Grid to copy from
        Grid srcGrid(10, 5);
        // setCell takes RGB components, not packed colors
        // setCell(col, row, glyphIndex, fgR, fgG, fgB, bgR, bgG, bgB)
        srcGrid.setCell(0, 0, 'H', 255, 255, 255, 0, 0, 0);
        srcGrid.setCell(1, 0, 'i', 255, 255, 255, 0, 0, 0);
        
        // Copy to shared memory back buffer
        grid->copyFromGrid(srcGrid);
        
        // After copy, data is in back buffer (not yet active)
        // We need to access back buffer to verify
        // For now, swap and check active buffer
        grid->swapBuffers();
        
        const uint32_t* codepoints = grid->activeCodepointData();
        expect(codepoints != nullptr);
        expect(codepoints[0] == uint32_t('H'));
        expect(codepoints[1] == uint32_t('i'));
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    "SharedGrid stores colors correctly"_test = [] {
        const char* shmName = "/test-shm-colors";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 10, 5);
        
        Grid srcGrid(10, 5);
        // FG: red, BG: blue
        srcGrid.setCell(0, 0, 'X', 255, 0, 0, 0, 0, 255);
        
        grid->copyFromGrid(srcGrid);
        grid->swapBuffers();
        
        const uint8_t* fg = grid->activeFgColorData();
        const uint8_t* bg = grid->activeBgColorData();
        
        // Each color is 4 bytes (RGBA)
        expect(fg[0] == 255_u) << "FG Red";
        expect(fg[1] == 0_u) << "FG Green";
        expect(fg[2] == 0_u) << "FG Blue";
        
        expect(bg[0] == 0_u) << "BG Red";
        expect(bg[1] == 0_u) << "BG Green";
        expect(bg[2] == 255_u) << "BG Blue";
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    //=========================================================================
    // Buffer header tests
    //=========================================================================
    
    "SharedGrid updateBackBuffer sets cursor position"_test = [] {
        const char* shmName = "/test-shm-cursor";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 80, 24);
        
        grid->updateBackBuffer(
            10,    // cursorRow
            20,    // cursorCol
            true,  // cursorVisible
            false, // isAltScreen
            false, // fullDamage
            0, 0, 24, 80,  // damage rect
            0      // scrollOffset
        );
        grid->swapBuffers();
        
        const auto* hdr = grid->activeBufferHeader();
        expect(hdr->cursorRow == 10_i);
        expect(hdr->cursorCol == 20_i);
        expect(hdr->cursorVisible == 1_u);
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    "SharedGrid updateBackBuffer sets damage rect"_test = [] {
        const char* shmName = "/test-shm-damage";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 80, 24);
        
        grid->updateBackBuffer(
            0, 0, true, false,
            true,  // fullDamage
            5, 10, 15, 70,  // damage rect: rows 5-15, cols 10-70
            0
        );
        grid->swapBuffers();
        
        const auto* hdr = grid->activeBufferHeader();
        expect(hdr->fullDamage == 1_u);
        expect(hdr->damageStartRow == 5_u);
        expect(hdr->damageStartCol == 10_u);
        expect(hdr->damageEndRow == 15_u);
        expect(hdr->damageEndCol == 70_u);
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    //=========================================================================
    // SharedGridView tests
    //=========================================================================
    
    "SharedGridView reads dimensions from SharedGrid"_test = [] {
        const char* shmName = "/test-shm-view-dims";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 120, 40);
        SharedGridView view(grid);
        
        expect(view.getCols() == 120_u);
        expect(view.getRows() == 40_u);
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    "SharedGridView sync detects sequence change"_test = [] {
        const char* shmName = "/test-shm-view-sync";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 10, 5);
        SharedGridView view(grid);
        
        // First sync should process
        Grid srcGrid(10, 5);
        srcGrid.setCell(0, 0, 'A', 255, 255, 255, 0, 0, 0);
        grid->copyFromGrid(srcGrid);
        grid->swapBuffers();
        
        view.syncFromSharedMemory();
        
        // Get glyph data - without Font, codepoint becomes glyph index
        const uint16_t* glyphs = view.getGlyphData();
        expect(glyphs != nullptr);
        // Without Font, codepoint 'A' (65) becomes glyph index 65
        expect(glyphs[0] == 65_u) << "Should have glyph index for 'A'";
        
        // Same sequence - sync should skip
        // (we can't easily test this without mocking, but at least verify no crash)
        view.syncFromSharedMemory();
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    "SharedGridView provides zero-copy color access"_test = [] {
        const char* shmName = "/test-shm-view-colors";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 10, 5);
        SharedGridView view(grid);
        
        Grid srcGrid(10, 5);
        srcGrid.setCell(0, 0, 'X', 0x12, 0x34, 0x56, 0x87, 0x65, 0x43);
        grid->copyFromGrid(srcGrid);
        grid->swapBuffers();
        
        // View should return pointers directly into shared memory
        const uint8_t* viewFg = view.getFgColorData();
        const uint8_t* gridFg = grid->activeFgColorData();
        
        expect(viewFg == gridFg) << "View should return same pointer (zero-copy)";
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    //=========================================================================
    // Concurrent access tests
    //=========================================================================
    
    "SharedGrid client sees server's buffer swap"_test = [] {
        const char* shmName = "/test-shm-concurrent";
        SharedGrid::unlink(shmName);
        
        auto* server = SharedGrid::createServer(shmName, 10, 5);
        auto* client = SharedGrid::openClient(shmName);
        
        // Server writes to back buffer and swaps
        Grid srcGrid(10, 5);
        srcGrid.setCell(0, 0, 'S', 255, 255, 255, 0, 0, 0);
        server->copyFromGrid(srcGrid);
        server->swapBuffers();
        
        // Client should see the swapped data
        const uint32_t* clientData = client->activeCodepointData();
        expect(clientData[0] == uint32_t('S')) << "Client should see server's data";
        
        // Server updates again
        srcGrid.setCell(0, 0, 'T', 255, 255, 255, 0, 0, 0);
        server->copyFromGrid(srcGrid);
        server->swapBuffers();
        
        // Client sees new data
        clientData = client->activeCodepointData();
        expect(clientData[0] == uint32_t('T')) << "Client should see updated data";
        
        delete client;
        delete server;
        SharedGrid::unlink(shmName);
    };
    
    //=========================================================================
    // Edge cases
    //=========================================================================
    
    "SharedGrid handles null codepoints as space"_test = [] {
        const char* shmName = "/test-shm-null";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, 10, 5);
        SharedGridView view(grid);
        
        // Empty grid has zeros
        Grid srcGrid(10, 5);
        // Don't set any cells - they should be 0
        grid->copyFromGrid(srcGrid);
        grid->swapBuffers();
        
        view.syncFromSharedMemory();
        
        // syncFromSharedMemory converts 0 to space before glyph lookup
        const uint16_t* glyphs = view.getGlyphData();
        expect(glyphs[0] == uint16_t(' ')) << "Null codepoint should become space glyph";
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
    
    "SharedGrid memory size calculation"_test = [] {
        // Verify the memory layout calculation
        // Header + 2 * (BufferHeader + codepoints + fg + bg + attrs)
        
        uint32_t cols = 80;
        uint32_t rows = 24;
        
        const char* shmName = "/test-shm-size";
        SharedGrid::unlink(shmName);
        
        auto* grid = SharedGrid::createServer(shmName, cols, rows);
        // We can't easily get the actual size, but we can verify it doesn't crash
        expect(grid != nullptr);
        
        // Access all parts to verify memory is accessible
        expect(grid->header() != nullptr);
        expect(grid->activeBufferHeader() != nullptr);
        expect(grid->activeCodepointData() != nullptr);
        expect(grid->activeFgColorData() != nullptr);
        expect(grid->activeBgColorData() != nullptr);
        expect(grid->activeAttrsData() != nullptr);
        
        delete grid;
        SharedGrid::unlink(shmName);
    };
};
