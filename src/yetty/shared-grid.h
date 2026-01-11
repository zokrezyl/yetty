#pragma once

#include "grid.h"
#include <atomic>
#include <cstdint>
#include <string>

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace yetty {

//=============================================================================
// SharedGrid - Double-buffered Grid in POSIX shared memory
//
// Memory layout:
//   [SharedGridHeader]
//   [Buffer 0: SharedGridBufferHeader + glyphs + fgColors + bgColors + attrs]
//   [Buffer 1: SharedGridBufferHeader + glyphs + fgColors + bgColors + attrs]
//
// Server writes to back buffer, then atomically swaps activeBuffer.
// Client reads from front buffer - zero copy, no locks needed.
//=============================================================================

// Double-buffered shared memory layout:
// Server writes to back buffer, then atomically swaps activeBuffer index.
// Client reads from front buffer (buffer[activeBuffer]).
// No locks needed - atomic swap ensures consistency.
//
// IMPORTANT: Shared memory stores CODEPOINTS (uint32_t), not glyph indices!
// The server doesn't have Font, so it stores Unicode codepoints.
// The client converts codepoints to glyph indices when rendering.

struct SharedGridHeader {
    uint32_t magic;          // 0x59455454 ('YETT')
    uint32_t version;        // 3 (double-buffered, codepoints)
    uint32_t cols;
    uint32_t rows;
    
    // Atomic buffer index: 0 or 1
    // Client reads buffer[activeBuffer]
    // Server writes to buffer[1 - activeBuffer], then swaps
    std::atomic<uint32_t> activeBuffer;
    
    // Per-buffer metadata (cursor, damage, etc.)
    // These are in the buffer data, not header
    
    uint32_t scrollbackSize;
    int32_t mouseMode;
    uint32_t _reserved[8];
};

// Per-buffer header (at start of each buffer)
struct SharedGridBufferHeader {
    int32_t cursorRow;
    int32_t cursorCol;
    uint8_t cursorVisible;
    uint8_t isAltScreen;
    uint8_t fullDamage;
    uint8_t _pad1;
    uint32_t damageStartRow;
    uint32_t damageStartCol;
    uint32_t damageEndRow;
    uint32_t damageEndCol;
    int32_t scrollOffset;
    uint32_t sequenceNumber;  // Incremented on each update
    uint32_t _pad2[2];
};

static constexpr uint32_t SHARED_GRID_MAGIC = 0x59455454;  // 'YETT'
static constexpr uint32_t SHARED_GRID_VERSION = 3;  // Double-buffered + codepoints

class SharedGrid {
public:
    // Calculate required shared memory size (for both buffers)
    static size_t calculateSize(uint32_t cols, uint32_t rows) {
        size_t bufferSize = calculateBufferSize(cols, rows);
        return sizeof(SharedGridHeader) + 2 * bufferSize;  // 2 buffers
    }
    
    // Size of one buffer (header + data)
    static size_t calculateBufferSize(uint32_t cols, uint32_t rows) {
        size_t cellCount = cols * rows;
        return sizeof(SharedGridBufferHeader) +
               cellCount * sizeof(uint32_t) +  // codepoints (NOT glyph indices!)
               cellCount * 4 +                  // fgColors (RGBA)
               cellCount * 4 +                  // bgColors (RGBA)
               cellCount;                       // attrs
    }

    // Server: Create and own shared memory
    static SharedGrid* createServer(const std::string& name, uint32_t cols, uint32_t rows);
    
    // Client: Open existing shared memory read-only
    static SharedGrid* openClient(const std::string& name);
    
    // Unlink shared memory (cleanup)
    static void unlink(const std::string& name);
    
    ~SharedGrid();

    // Non-copyable
    SharedGrid(const SharedGrid&) = delete;
    SharedGrid& operator=(const SharedGrid&) = delete;

    // Check if valid
    bool isValid() const { return ptr_ != nullptr && ptr_ != MAP_FAILED; }
    bool isServer() const { return isServer_; }

    // Header access
    SharedGridHeader* header() { return reinterpret_cast<SharedGridHeader*>(ptr_); }
    const SharedGridHeader* header() const { return reinterpret_cast<const SharedGridHeader*>(ptr_); }

    // Grid dimensions (from header)
    uint32_t getCols() const { return header()->cols; }
    uint32_t getRows() const { return header()->rows; }

    //=========================================================================
    // Double-buffer access
    //=========================================================================
    
    // Get active buffer index (client should read this buffer)
    uint32_t getActiveBuffer() const { 
        return header()->activeBuffer.load(std::memory_order_acquire); 
    }
    
    // Get buffer header for specified buffer (0 or 1)
    SharedGridBufferHeader* bufferHeader(uint32_t bufferIndex) {
        char* base = static_cast<char*>(ptr_) + sizeof(SharedGridHeader);
        size_t bufferSize = calculateBufferSize(getCols(), getRows());
        return reinterpret_cast<SharedGridBufferHeader*>(base + bufferIndex * bufferSize);
    }
    const SharedGridBufferHeader* bufferHeader(uint32_t bufferIndex) const {
        const char* base = static_cast<const char*>(ptr_) + sizeof(SharedGridHeader);
        size_t bufferSize = calculateBufferSize(getCols(), getRows());
        return reinterpret_cast<const SharedGridBufferHeader*>(base + bufferIndex * bufferSize);
    }
    
    // Get active buffer header (for client reading)
    const SharedGridBufferHeader* activeBufferHeader() const {
        return bufferHeader(getActiveBuffer());
    }
    
    // Get back buffer header (for server writing)
    SharedGridBufferHeader* backBufferHeader() {
        return bufferHeader(1 - getActiveBuffer());
    }

    // Data pointers for specified buffer - CODEPOINTS (uint32_t), not glyph indices!
    uint32_t* codepointData(uint32_t bufferIndex) {
        char* bufBase = reinterpret_cast<char*>(bufferHeader(bufferIndex));
        return reinterpret_cast<uint32_t*>(bufBase + sizeof(SharedGridBufferHeader));
    }
    const uint32_t* codepointData(uint32_t bufferIndex) const {
        const char* bufBase = reinterpret_cast<const char*>(bufferHeader(bufferIndex));
        return reinterpret_cast<const uint32_t*>(bufBase + sizeof(SharedGridBufferHeader));
    }

    uint8_t* fgColorData(uint32_t bufferIndex) {
        char* bufBase = reinterpret_cast<char*>(bufferHeader(bufferIndex));
        size_t offset = sizeof(SharedGridBufferHeader) + getCols() * getRows() * sizeof(uint32_t);
        return reinterpret_cast<uint8_t*>(bufBase + offset);
    }
    const uint8_t* fgColorData(uint32_t bufferIndex) const {
        const char* bufBase = reinterpret_cast<const char*>(bufferHeader(bufferIndex));
        size_t offset = sizeof(SharedGridBufferHeader) + getCols() * getRows() * sizeof(uint32_t);
        return reinterpret_cast<const uint8_t*>(bufBase + offset);
    }

    uint8_t* bgColorData(uint32_t bufferIndex) {
        char* bufBase = reinterpret_cast<char*>(bufferHeader(bufferIndex));
        size_t offset = sizeof(SharedGridBufferHeader) + 
                        getCols() * getRows() * sizeof(uint32_t) +
                        getCols() * getRows() * 4;
        return reinterpret_cast<uint8_t*>(bufBase + offset);
    }
    const uint8_t* bgColorData(uint32_t bufferIndex) const {
        const char* bufBase = reinterpret_cast<const char*>(bufferHeader(bufferIndex));
        size_t offset = sizeof(SharedGridBufferHeader) + 
                        getCols() * getRows() * sizeof(uint32_t) +
                        getCols() * getRows() * 4;
        return reinterpret_cast<const uint8_t*>(bufBase + offset);
    }

    uint8_t* attrsData(uint32_t bufferIndex) {
        char* bufBase = reinterpret_cast<char*>(bufferHeader(bufferIndex));
        size_t offset = sizeof(SharedGridBufferHeader) + 
                        getCols() * getRows() * sizeof(uint32_t) +
                        getCols() * getRows() * 4 +
                        getCols() * getRows() * 4;
        return reinterpret_cast<uint8_t*>(bufBase + offset);
    }
    const uint8_t* attrsData(uint32_t bufferIndex) const {
        const char* bufBase = reinterpret_cast<const char*>(bufferHeader(bufferIndex));
        size_t offset = sizeof(SharedGridBufferHeader) + 
                        getCols() * getRows() * sizeof(uint32_t) +
                        getCols() * getRows() * 4 +
                        getCols() * getRows() * 4;
        return reinterpret_cast<const uint8_t*>(bufBase + offset);
    }
    
    // Convenience: Get active buffer data (for client)
    const uint32_t* activeCodepointData() const { return codepointData(getActiveBuffer()); }
    const uint8_t* activeFgColorData() const { return fgColorData(getActiveBuffer()); }
    const uint8_t* activeBgColorData() const { return bgColorData(getActiveBuffer()); }
    const uint8_t* activeAttrsData() const { return attrsData(getActiveBuffer()); }
    
    // Convenience: Get back buffer data (for server)
    uint32_t* backCodepointData() { return codepointData(1 - getActiveBuffer()); }
    uint8_t* backFgColorData() { return fgColorData(1 - getActiveBuffer()); }
    uint8_t* backBgColorData() { return bgColorData(1 - getActiveBuffer()); }
    uint8_t* backAttrsData() { return attrsData(1 - getActiveBuffer()); }

    //=========================================================================
    // Server operations
    //=========================================================================

    // Server: Copy codepoints from Grid to back buffer
    // Note: Grid stores glyph indices, but server's "glyph indices" are actually codepoints
    void copyFromGrid(const Grid& grid);

    // Server: Update back buffer header
    void updateBackBuffer(int cursorRow, int cursorCol, bool cursorVisible,
                          bool isAltScreen, bool fullDamage,
                          uint32_t damageStartRow, uint32_t damageStartCol,
                          uint32_t damageEndRow, uint32_t damageEndCol,
                          int scrollOffset);

    // Server: Swap buffers (makes back buffer active for clients)
    void swapBuffers() {
        uint32_t current = header()->activeBuffer.load(std::memory_order_relaxed);
        header()->activeBuffer.store(1 - current, std::memory_order_release);
    }

    // Get shared memory name
    const std::string& getName() const { return name_; }

private:
    SharedGrid() = default;

    std::string name_;
    void* ptr_ = nullptr;
    size_t size_ = 0;
    int fd_ = -1;
    bool isServer_ = false;
};

//=============================================================================
// SharedGridView - Grid subclass that reads from SharedGrid
//
// This provides a Grid interface for GridRenderer.
// Codepoints are converted to glyph indices using Font on each sync.
// Colors and attrs are zero-copy from shared memory.
//=============================================================================

class Font;  // Forward declaration

class SharedGridView : public Grid {
public:
    explicit SharedGridView(SharedGrid* sharedGrid, Font* font = nullptr)
        : Grid(0, 0)  // Don't allocate storage in base
        , sharedGrid_(sharedGrid)
        , font_(font) {
        if (sharedGrid_) {
            // Allocate glyph index buffer for conversion
            size_t cellCount = sharedGrid_->getCols() * sharedGrid_->getRows();
            glyphIndices_.resize(cellCount, 0);
        }
    }
    
    void setFont(Font* font) { font_ = font; }
    
    // Sync: convert codepoints to glyph indices
    // Call this before rendering!
    void syncFromSharedMemory();
    
    // Override to read from shared memory
    uint32_t getCols() const override { 
        return sharedGrid_ ? sharedGrid_->getCols() : 0; 
    }
    uint32_t getRows() const override { 
        return sharedGrid_ ? sharedGrid_->getRows() : 0; 
    }
    
    // Glyph data - from our converted buffer
    const uint16_t* getGlyphData() const override { 
        return glyphIndices_.empty() ? nullptr : glyphIndices_.data(); 
    }
    
    // Colors and attrs - zero-copy from shared memory
    const uint8_t* getFgColorData() const override { 
        return sharedGrid_ ? sharedGrid_->activeFgColorData() : nullptr; 
    }
    const uint8_t* getBgColorData() const override { 
        return sharedGrid_ ? sharedGrid_->activeBgColorData() : nullptr; 
    }
    const uint8_t* getAttrsData() const override { 
        return sharedGrid_ ? sharedGrid_->activeAttrsData() : nullptr; 
    }
    
    // Access to buffer header for cursor/damage info
    const SharedGridBufferHeader* bufferHeader() const {
        return sharedGrid_ ? sharedGrid_->activeBufferHeader() : nullptr;
    }
    
    // Access to raw codepoints (for debugging or special processing)
    const uint32_t* getCodepointData() const {
        return sharedGrid_ ? sharedGrid_->activeCodepointData() : nullptr;
    }
    
    SharedGrid* sharedGrid() const { return sharedGrid_; }

private:
    SharedGrid* sharedGrid_;
    Font* font_ = nullptr;
    std::vector<uint16_t> glyphIndices_;  // Converted from codepoints
    uint32_t lastSequence_ = 0;  // Track if we need to re-convert
};

} // namespace yetty
