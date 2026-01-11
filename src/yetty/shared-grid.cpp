#include "shared-grid.h"
#include <ytrace/ytrace.hpp>
#include <cstring>

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace yetty {

SharedGrid* SharedGrid::createServer(const std::string& name, uint32_t cols, uint32_t rows) {
#ifndef _WIN32
    auto* sg = new SharedGrid();
    sg->name_ = name;
    sg->isServer_ = true;
    sg->size_ = calculateSize(cols, rows);

    // Remove existing if any
    shm_unlink(name.c_str());

    // Create shared memory
    sg->fd_ = shm_open(name.c_str(), O_CREAT | O_RDWR, 0666);
    if (sg->fd_ < 0) {
        yerror("SharedGrid: shm_open failed: {}", strerror(errno));
        delete sg;
        return nullptr;
    }

    // Set size
    if (ftruncate(sg->fd_, sg->size_) < 0) {
        yerror("SharedGrid: ftruncate failed: {}", strerror(errno));
        close(sg->fd_);
        shm_unlink(name.c_str());
        delete sg;
        return nullptr;
    }

    // Map
    sg->ptr_ = mmap(nullptr, sg->size_, PROT_READ | PROT_WRITE, MAP_SHARED, sg->fd_, 0);
    if (sg->ptr_ == MAP_FAILED) {
        yerror("SharedGrid: mmap failed: {}", strerror(errno));
        close(sg->fd_);
        shm_unlink(name.c_str());
        delete sg;
        return nullptr;
    }

    // Initialize header
    memset(sg->ptr_, 0, sg->size_);
    auto* hdr = sg->header();
    hdr->magic = SHARED_GRID_MAGIC;
    hdr->version = SHARED_GRID_VERSION;
    hdr->cols = cols;
    hdr->rows = rows;
    hdr->activeBuffer.store(0, std::memory_order_relaxed);
    
    // Initialize both buffer headers
    for (int i = 0; i < 2; i++) {
        auto* bufHdr = sg->bufferHeader(i);
        bufHdr->cursorVisible = 1;
        bufHdr->fullDamage = 1;
        bufHdr->sequenceNumber = 0;
    }

    yinfo("SharedGrid: created {} ({}x{}, {} bytes, double-buffered)", 
                 name, cols, rows, sg->size_);
    return sg;
#else
    yerror("SharedGrid: Windows not implemented");
    return nullptr;
#endif
}

SharedGrid* SharedGrid::openClient(const std::string& name) {
#ifndef _WIN32
    auto* sg = new SharedGrid();
    sg->name_ = name;
    sg->isServer_ = false;

    // Open existing shared memory
    sg->fd_ = shm_open(name.c_str(), O_RDONLY, 0);
    if (sg->fd_ < 0) {
        yerror("SharedGrid: shm_open failed (client): {}", strerror(errno));
        delete sg;
        return nullptr;
    }

    // Get size
    struct stat st;
    if (fstat(sg->fd_, &st) < 0) {
        yerror("SharedGrid: fstat failed: {}", strerror(errno));
        close(sg->fd_);
        delete sg;
        return nullptr;
    }
    sg->size_ = st.st_size;

    // Map read-only
    sg->ptr_ = mmap(nullptr, sg->size_, PROT_READ, MAP_SHARED, sg->fd_, 0);
    if (sg->ptr_ == MAP_FAILED) {
        yerror("SharedGrid: mmap failed (client): {}", strerror(errno));
        close(sg->fd_);
        delete sg;
        return nullptr;
    }

    // Validate header
    auto* hdr = sg->header();
    if (hdr->magic != SHARED_GRID_MAGIC) {
        yerror("SharedGrid: invalid magic: {:08x}", hdr->magic);
        munmap(sg->ptr_, sg->size_);
        close(sg->fd_);
        delete sg;
        return nullptr;
    }
    if (hdr->version != SHARED_GRID_VERSION) {
        yerror("SharedGrid: version mismatch: {} vs {}", hdr->version, SHARED_GRID_VERSION);
        munmap(sg->ptr_, sg->size_);
        close(sg->fd_);
        delete sg;
        return nullptr;
    }

    yinfo("SharedGrid: opened {} ({}x{}, {} bytes, double-buffered)", 
                 name, hdr->cols, hdr->rows, sg->size_);
    return sg;
#else
    yerror("SharedGrid: Windows not implemented");
    return nullptr;
#endif
}

void SharedGrid::unlink(const std::string& name) {
#ifndef _WIN32
    shm_unlink(name.c_str());
#endif
}

SharedGrid::~SharedGrid() {
#ifndef _WIN32
    if (ptr_ && ptr_ != MAP_FAILED) {
        munmap(ptr_, size_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
    if (isServer_ && !name_.empty()) {
        shm_unlink(name_.c_str());
        yinfo("SharedGrid: unlinked {}", name_);
    }
#endif
}

void SharedGrid::copyFromGrid(const Grid& grid) {
    if (!isServer_ || !isValid()) return;

    uint32_t cols = getCols();
    uint32_t rows = getRows();
    
    if (grid.getCols() != cols || grid.getRows() != rows) {
        ywarn("SharedGrid::copyFromGrid: size mismatch {}x{} vs {}x{}",
                     grid.getCols(), grid.getRows(), cols, rows);
        return;
    }

    size_t cellCount = cols * rows;
    uint32_t backBuf = 1 - getActiveBuffer();

    // Copy glyph indices as codepoints (server's glyph indices ARE codepoints)
    // Need to expand uint16_t to uint32_t
    const uint16_t* srcGlyphs = grid.getGlyphData();
    uint32_t* dstCodepoints = codepointData(backBuf);
    for (size_t i = 0; i < cellCount; i++) {
        dstCodepoints[i] = srcGlyphs[i];
    }
    
    // Copy colors and attrs directly
    memcpy(fgColorData(backBuf), grid.getFgColorData(), cellCount * 4);
    memcpy(bgColorData(backBuf), grid.getBgColorData(), cellCount * 4);
    memcpy(attrsData(backBuf), grid.getAttrsData(), cellCount);
    
    // Increment sequence in back buffer
    bufferHeader(backBuf)->sequenceNumber++;
}

void SharedGrid::updateBackBuffer(int cursorRow, int cursorCol, bool cursorVisible,
                                  bool isAltScreen, bool fullDamage,
                                  uint32_t damageStartRow, uint32_t damageStartCol,
                                  uint32_t damageEndRow, uint32_t damageEndCol,
                                  int scrollOffset) {
    if (!isServer_ || !isValid()) return;

    auto* bufHdr = backBufferHeader();
    bufHdr->cursorRow = cursorRow;
    bufHdr->cursorCol = cursorCol;
    bufHdr->cursorVisible = cursorVisible ? 1 : 0;
    bufHdr->isAltScreen = isAltScreen ? 1 : 0;
    bufHdr->fullDamage = fullDamage ? 1 : 0;
    bufHdr->damageStartRow = damageStartRow;
    bufHdr->damageStartCol = damageStartCol;
    bufHdr->damageEndRow = damageEndRow;
    bufHdr->damageEndCol = damageEndCol;
    bufHdr->scrollOffset = scrollOffset;
}

//=============================================================================
// SharedGridView implementation
//=============================================================================

} // namespace yetty

// Include Font here to avoid circular dependency in header
#ifndef YETTY_SERVER_BUILD
#include <yetty/font.h>
#include "yetty/emoji.h"
#endif

namespace yetty {

void SharedGridView::syncFromSharedMemory() {
    if (!sharedGrid_) return;
    
    const auto* bufHdr = sharedGrid_->activeBufferHeader();
    if (!bufHdr) return;
    
    // Check if data has changed
    if (bufHdr->sequenceNumber == lastSequence_) {
        return;  // No change, skip conversion
    }
    
    ydebug("SharedGridView::sync seq {} -> {}, font={}", 
                  lastSequence_, bufHdr->sequenceNumber, font_ != nullptr);
    lastSequence_ = bufHdr->sequenceNumber;
    
    uint32_t cols = sharedGrid_->getCols();
    uint32_t rows = sharedGrid_->getRows();
    size_t cellCount = cols * rows;
    
    // Resize if needed
    if (glyphIndices_.size() != cellCount) {
        glyphIndices_.resize(cellCount);
    }
    
    const uint32_t* codepoints = sharedGrid_->activeCodepointData();
    const uint8_t* attrs = sharedGrid_->activeAttrsData();
    
    // Convert codepoints to glyph indices
    for (size_t i = 0; i < cellCount; i++) {
        uint32_t cp = codepoints[i];
        if (cp == 0) cp = ' ';  // Null -> space
        
        // Check attrs for bold/italic
        uint8_t attr = attrs[i];
        bool isBold = (attr & 0x01) != 0;
        bool isItalic = (attr & 0x02) != 0;
        
#ifndef YETTY_SERVER_BUILD
        if (font_) {
            glyphIndices_[i] = font_->getGlyphIndex(cp, isBold, isItalic);
        } else {
            // No font - use codepoint directly (won't render correctly but won't crash)
            glyphIndices_[i] = static_cast<uint16_t>(cp & 0xFFFF);
        }
#else
        // Server build - shouldn't call this
        glyphIndices_[i] = static_cast<uint16_t>(cp & 0xFFFF);
#endif
    }
}

} // namespace yetty
