#include "remote-terminal-backend.h"
#include "shared-grid.h"
#include <yetty/font.h>

#include <spdlog/spdlog.h>
#include <cstring>
#include <vector>
#include <chrono>

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/limits.h>
#endif

namespace yetty {

//=============================================================================
// Factory
//=============================================================================

Result<RemoteTerminalBackend::Ptr> RemoteTerminalBackend::create(
    uint32_t cols, uint32_t rows, uv_loop_t* loop,
    const std::string& serverSocketPath) noexcept {
    
    if (!loop) {
        return Err<Ptr>("RemoteTerminalBackend::create: null libuv loop");
    }
    
    auto backend = Ptr(new RemoteTerminalBackend(cols, rows, loop));
    if (auto res = backend->init(serverSocketPath); !res) {
        return Err<Ptr>("Failed to initialize RemoteTerminalBackend", res);
    }
    return Ok(std::move(backend));
}

RemoteTerminalBackend::RemoteTerminalBackend(
    uint32_t cols, uint32_t rows, uv_loop_t* loop) noexcept
    : loop_(loop)
    , cols_(cols)
    , rows_(rows) {
    
    // Create local grid as fallback until we connect to server
    localGrid_ = std::make_unique<Grid>(cols, rows);
    grid_ = localGrid_.get();
}

Result<void> RemoteTerminalBackend::init(const std::string& serverSocketPath) noexcept {
    // Default socket path
    if (serverSocketPath.empty()) {
        const char* xdgRuntime = getenv("XDG_RUNTIME_DIR");
        if (xdgRuntime) {
            socketPath_ = std::string(xdgRuntime) + "/yetty-server.sock";
        } else {
            socketPath_ = "/tmp/yetty-server.sock";
        }
    } else {
        socketPath_ = serverSocketPath;
    }
    
    // Default shared memory name
    shmName_ = "/yetty-grid-0";
    
    readBuffer_ = std::make_unique<char[]>(READ_BUFFER_SIZE);
    
    spdlog::info("RemoteTerminalBackend: will connect to {}", socketPath_);
    return Ok();
}

RemoteTerminalBackend::~RemoteTerminalBackend() {
    stop();
    // SharedGrid cleanup is automatic via unique_ptr
}

//=============================================================================
// Lifecycle
//=============================================================================

Result<void> RemoteTerminalBackend::start(const std::string& shell) {
    if (running_) return Ok();
    
    spdlog::info("RemoteTerminalBackend: starting (shell='{}')", shell);
    
    // Try to connect to existing server
    if (auto res = connectToServer(); !res) {
        spdlog::info("RemoteTerminalBackend: server not running, spawning yetty-server...");
        
        // Spawn server
        if (auto spawnRes = spawnServer(shell); !spawnRes) {
            return Err<void>("Failed to spawn yetty-server", spawnRes);
        }
        
        // Wait for server to start and create socket
        // Poll for socket existence with timeout
        int wait_ms = 0;
        const int max_wait_ms = 3000;  // 3 seconds max
        while (wait_ms < max_wait_ms) {
            struct stat st;
            if (stat(socketPath_.c_str(), &st) == 0) {
                break;  // Socket exists
            }
            usleep(50000);  // 50ms
            wait_ms += 50;
        }
        
        if (wait_ms >= max_wait_ms) {
            return Err<void>("Server did not create socket within timeout");
        }
        
        // Give server a moment more to be ready to accept
        usleep(100000);  // 100ms
        
        if (auto res2 = connectToServer(); !res2) {
            return Err<void>("Failed to connect after spawning server", res2);
        }
    }
    
    // Map shared memory Grid
    if (auto res = mapSharedGrid(); !res) {
        return Err<void>("Failed to map shared grid", res);
    }
    
    running_ = true;
    fullDamage_ = true;
    
    // Tell server our desired grid size (client dictates size, not server)
    resize(cols_, rows_);
    
    spdlog::info("RemoteTerminalBackend: connected to server, grid mapped");
    return Ok();
}

void RemoteTerminalBackend::stop() {
    if (!running_) return;
    
    running_ = false;
    
    if (socket_) {
        uv_close(reinterpret_cast<uv_handle_t*>(socket_), [](uv_handle_t* h) {
            delete reinterpret_cast<uv_pipe_t*>(h);
        });
        socket_ = nullptr;
    }
    connected_ = false;
}

//=============================================================================
// Server spawning
//=============================================================================

Result<void> RemoteTerminalBackend::spawnServer(const std::string& shell) noexcept {
#ifndef _WIN32
    // Find yetty-server binary - same directory as yetty
    std::string serverPath;
    
    // Try to find it relative to /proc/self/exe
    char exePath[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        std::string dir(exePath);
        size_t lastSlash = dir.rfind('/');
        if (lastSlash != std::string::npos) {
            serverPath = dir.substr(0, lastSlash + 1) + "yetty-server";
        }
    }
    
    if (serverPath.empty() || access(serverPath.c_str(), X_OK) != 0) {
        // Try PATH
        serverPath = "yetty-server";
    }
    
    spdlog::info("RemoteTerminalBackend: spawning {}", serverPath);
    
    pid_t pid = fork();
    if (pid < 0) {
        return Err<void>("fork failed: " + std::string(strerror(errno)));
    }
    
    if (pid == 0) {
        // Child - become session leader and exec server
        setsid();
        
        // Close stdin/stdout/stderr to detach from terminal
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        
        // Redirect stdin to /dev/null
        open("/dev/null", O_RDONLY);  // stdin (fd 0)
        
        // Redirect stdout/stderr to log file for debugging
        int logfd = open("/tmp/yetty-server.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0) {
            dup2(logfd, STDOUT_FILENO);  // stdout
            dup2(logfd, STDERR_FILENO);  // stderr
            if (logfd > STDERR_FILENO) close(logfd);
        } else {
            open("/dev/null", O_WRONLY);  // stdout
            open("/dev/null", O_WRONLY);  // stderr
        }
        
        // Build args - strings must stay alive until execvp
        std::string colsStr = std::to_string(cols_);
        std::string rowsStr = std::to_string(rows_);
        
        std::vector<const char*> args;
        args.push_back(serverPath.c_str());
        args.push_back("-s");
        args.push_back(socketPath_.c_str());
        args.push_back("-m");
        args.push_back(shmName_.c_str());
        args.push_back("-c");
        args.push_back(colsStr.c_str());
        args.push_back("-r");
        args.push_back(rowsStr.c_str());
        if (!shell.empty()) {
            args.push_back("-e");
            args.push_back(shell.c_str());
        }
        args.push_back(nullptr);
        
        execvp(serverPath.c_str(), const_cast<char* const*>(args.data()));
        _exit(1);  // exec failed
    }
    
    // Parent - server spawned
    serverPid_ = pid;
    spdlog::info("RemoteTerminalBackend: spawned server PID {}", pid);
    return Ok();
#else
    return Err<void>("Windows not implemented");
#endif
}

//=============================================================================
// Connection
//=============================================================================

Result<void> RemoteTerminalBackend::connectToServer() noexcept {
#ifndef _WIN32
    // Check if socket file exists first
    struct stat st;
    if (stat(socketPath_.c_str(), &st) != 0) {
        return Err<void>("Server socket does not exist: " + socketPath_);
    }
    
    socket_ = new uv_pipe_t;
    uv_pipe_init(loop_, socket_, 0);
    socket_->data = this;
    
    auto* connectReq = new uv_connect_t;
    connectReq->data = this;
    
    // Start async connect
    uv_pipe_connect(connectReq, socket_, socketPath_.c_str(), onConnect);
    
    // Run event loop until connected or error
    // Use a timeout to avoid hanging forever
    connected_ = false;
    int timeout_ms = 2000;  // 2 second timeout
    auto start = std::chrono::steady_clock::now();
    
    while (!connected_) {
        uv_run(loop_, UV_RUN_NOWAIT);
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeout_ms) {
            if (socket_) {
                uv_close(reinterpret_cast<uv_handle_t*>(socket_), [](uv_handle_t* h) {
                    delete reinterpret_cast<uv_pipe_t*>(h);
                });
                socket_ = nullptr;
            }
            return Err<void>("Connection timeout after " + std::to_string(timeout_ms) + "ms");
        }
        
        // Check if connection failed
        if (!socket_) {
            return Err<void>("Connection failed");
        }
        
        usleep(10000);  // 10ms sleep between polls
    }
    
    return Ok();
#else
    return Err<void>("Windows not implemented");
#endif
}

Result<void> RemoteTerminalBackend::mapSharedGrid() noexcept {
#ifndef _WIN32
    // Shared memory name is provided by server after connection
    // For now, use a default name
    if (shmName_.empty()) {
        shmName_ = "/yetty-grid-0";
    }
    
    // Open shared memory as client (read-only)
    SharedGrid* sg = SharedGrid::openClient(shmName_);
    if (!sg) {
        return Err<void>("Failed to open shared memory: " + shmName_);
    }
    
    // Take ownership
    sharedGrid_.reset(sg);
    
    // Create view that provides Grid interface
    sharedGridView_ = std::make_unique<SharedGridView>(sharedGrid_.get());
    
    // Switch to using shared memory grid (zero-copy!)
    grid_ = sharedGridView_.get();
    
    // Update dimensions from shared memory
    cols_ = sharedGrid_->getCols();
    rows_ = sharedGrid_->getRows();
    
    spdlog::info("RemoteTerminalBackend: using SharedGridView for zero-copy access to {}x{} grid", 
                 cols_, rows_);
    
    return Ok();
#else
    return Err<void>("Windows not implemented");
#endif
}

//=============================================================================
// Input
//=============================================================================

void RemoteTerminalBackend::sendKey(uint32_t codepoint, VTermModifier mod) {
    if (!connected_) return;
    
    // Reset scroll on input
    if (scrollOffset_ != 0) {
        scrollOffset_ = 0;
        fullDamage_ = true;
    }
    
    // Format: "KEY <codepoint> <mod>\n"
    std::string cmd = "KEY " + std::to_string(codepoint) + " " + std::to_string(mod) + "\n";
    sendCommand(cmd);
}

void RemoteTerminalBackend::sendSpecialKey(VTermKey key, VTermModifier mod) {
    if (!connected_) return;
    
    if (scrollOffset_ != 0) {
        scrollOffset_ = 0;
        fullDamage_ = true;
    }
    
    // Format: "SPECIAL <key> <mod>\n"
    std::string cmd = "SPECIAL " + std::to_string(key) + " " + std::to_string(mod) + "\n";
    sendCommand(cmd);
}

void RemoteTerminalBackend::sendRaw(const char* data, size_t len) {
    if (!connected_) return;
    
    if (scrollOffset_ != 0) {
        scrollOffset_ = 0;
        fullDamage_ = true;
    }
    
    // Format: "RAW <len>\n<data>"
    std::string cmd = "RAW " + std::to_string(len) + "\n";
    sendCommand(cmd);
    
    // Send raw data
    if (socket_ && len > 0) {
        uv_buf_t buf = uv_buf_init(const_cast<char*>(data), len);
        uv_write_t* req = new uv_write_t;
        uv_write(req, reinterpret_cast<uv_stream_t*>(socket_), &buf, 1,
                 [](uv_write_t* req, int) { delete req; });
    }
}

void RemoteTerminalBackend::resize(uint32_t cols, uint32_t rows) {
    cols_ = cols;
    rows_ = rows;
    localGrid_->resize(cols, rows);
    
    if (connected_) {
        // Format: "RESIZE <cols> <rows>\n"
        std::string cmd = "RESIZE " + std::to_string(cols) + " " + std::to_string(rows) + "\n";
        sendCommand(cmd);
    }
    
    fullDamage_ = true;
}

//=============================================================================
// Scrollback
//=============================================================================

void RemoteTerminalBackend::scrollUp(int lines) {
    if (connected_) {
        std::string cmd = "SCROLL " + std::to_string(lines) + "\n";
        sendCommand(cmd);
    }
    scrollOffset_ += lines;
    fullDamage_ = true;
}

void RemoteTerminalBackend::scrollDown(int lines) {
    if (connected_) {
        std::string cmd = "SCROLL " + std::to_string(-lines) + "\n";
        sendCommand(cmd);
    }
    scrollOffset_ = std::max(0, scrollOffset_ - lines);
    fullDamage_ = true;
}

void RemoteTerminalBackend::scrollToTop() {
    if (connected_) {
        sendCommand("SCROLL_TOP\n");
    }
    scrollOffset_ = static_cast<int>(scrollbackSize_);
    fullDamage_ = true;
}

void RemoteTerminalBackend::scrollToBottom() {
    if (connected_) {
        sendCommand("SCROLL_BOTTOM\n");
    }
    scrollOffset_ = 0;
    fullDamage_ = true;
}

//=============================================================================
// Selection
//=============================================================================

void RemoteTerminalBackend::startSelection(int row, int col, SelectionMode mode) {
    selectionStart_ = {row, col};
    selectionEnd_ = {row, col};
    selectionMode_ = mode;
    fullDamage_ = true;
    
    if (connected_) {
        std::string cmd = "SELECT_START " + std::to_string(row) + " " + 
                          std::to_string(col) + " " + std::to_string(static_cast<int>(mode)) + "\n";
        sendCommand(cmd);
    }
}

void RemoteTerminalBackend::extendSelection(int row, int col) {
    if (selectionMode_ == SelectionMode::None) return;
    selectionEnd_ = {row, col};
    fullDamage_ = true;
    
    if (connected_) {
        std::string cmd = "SELECT_EXTEND " + std::to_string(row) + " " + std::to_string(col) + "\n";
        sendCommand(cmd);
    }
}

void RemoteTerminalBackend::clearSelection() {
    selectionMode_ = SelectionMode::None;
    fullDamage_ = true;
    
    if (connected_) {
        sendCommand("SELECT_CLEAR\n");
    }
}

bool RemoteTerminalBackend::isInSelection(int row, int col) const {
    if (selectionMode_ == SelectionMode::None) return false;

    VTermPos start = selectionStart_, end = selectionEnd_;
    if (vterm_pos_cmp(start, end) > 0) std::swap(start, end);

    VTermPos pos = {row, col};
    return vterm_pos_cmp(pos, start) >= 0 && vterm_pos_cmp(pos, end) <= 0;
}

std::string RemoteTerminalBackend::getSelectedText() {
    // TODO: Request from server
    if (!connected_) return "";
    
    // For now, return empty - server will need to provide this
    spdlog::warn("RemoteTerminalBackend::getSelectedText: TODO - request from server");
    return "";
}

//=============================================================================
// Grid sync
//=============================================================================

void RemoteTerminalBackend::setFont(Font* font) {
    font_ = font;
    if (sharedGridView_) {
        sharedGridView_->setFont(font);
    }
}

void RemoteTerminalBackend::syncToGrid() {
    // Sync codepoints to glyph indices
    if (sharedGridView_) {
        sharedGridView_->syncFromSharedMemory();
        
        const auto* bufHdr = sharedGridView_->bufferHeader();
        if (bufHdr) {
            cursorRow_ = bufHdr->cursorRow;
            cursorCol_ = bufHdr->cursorCol;
            cursorVisible_ = bufHdr->cursorVisible != 0;
            isAltScreen_ = bufHdr->isAltScreen != 0;
            scrollOffset_ = bufHdr->scrollOffset;
            
            // Update damage state
            if (bufHdr->fullDamage) {
                fullDamage_ = true;
            }
        }
    }
}

//=============================================================================
// IPC
//=============================================================================

void RemoteTerminalBackend::sendCommand(const std::string& cmd) {
    if (!socket_ || !connected_) return;
    
    char* data = new char[cmd.size()];
    memcpy(data, cmd.c_str(), cmd.size());
    
    uv_buf_t buf = uv_buf_init(data, cmd.size());
    uv_write_t* req = new uv_write_t;
    req->data = data;
    
    uv_write(req, reinterpret_cast<uv_stream_t*>(socket_), &buf, 1,
             [](uv_write_t* req, int status) {
                 if (status < 0) {
                     spdlog::error("RemoteTerminalBackend: write error: {}", uv_strerror(status));
                 }
                 delete[] static_cast<char*>(req->data);
                 delete req;
             });
}

void RemoteTerminalBackend::onServerMessage(const char* data, size_t len) {
    // Parse server messages
    // Format examples:
    //   "DAMAGE <seq> <fullDamage> <startRow> <startCol> <endRow> <endCol> <cursorRow> <cursorCol> <cursorVisible>\n"
    //   "CONNECTED <shmName> <cols> <rows>\n"
    //   "CURSOR <row> <col> <visible>\n"
    //   "ALTSCREEN <0|1>\n"
    //   "SCROLLBACK <size>\n"
    
    std::string msg(data, len);
    spdlog::debug("RemoteTerminalBackend: received: {}", msg);
    
    // Handle multiple messages (may be buffered together)
    size_t pos = 0;
    while (pos < msg.size()) {
        size_t end = msg.find('\n', pos);
        if (end == std::string::npos) end = msg.size();
        
        std::string line = msg.substr(pos, end - pos);
        pos = end + 1;
        
        if (line.empty()) continue;
        
        if (line.rfind("DAMAGE ", 0) == 0) {
            // Parse: DAMAGE <seq> <fullDamage> <startRow> <startCol> <endRow> <endCol> <cursorRow> <cursorCol> <cursorVisible>
            unsigned seq, startRow, startCol, endRow, endCol;
            int full, curRow, curCol, curVis;
            if (sscanf(line.c_str(), "DAMAGE %u %d %u %u %u %u %d %d %d",
                       &seq, &full, &startRow, &startCol, &endRow, &endCol,
                       &curRow, &curCol, &curVis) >= 6) {
                // Add damage rect
                if (full) {
                    fullDamage_ = true;
                } else if (endRow > startRow || endCol > startCol) {
                    DamageRect rect;
                    rect._startRow = startRow;
                    rect._startCol = startCol;
                    rect._endRow = endRow;
                    rect._endCol = endCol;
                    damageRects_.push_back(rect);
                }
                // Update cursor
                cursorRow_ = curRow;
                cursorCol_ = curCol;
                cursorVisible_ = curVis != 0;
            }
        } else if (line.rfind("CONNECTED ", 0) == 0) {
            // Already handled during connection setup
            spdlog::debug("RemoteTerminalBackend: server confirmed connection");
        } else if (line.rfind("RESIZED ", 0) == 0) {
            // Server resized the grid, need to remap shared memory
            // Format: RESIZED <shmName> <cols> <rows>
            char shmName[256];
            uint32_t newCols, newRows;
            if (sscanf(line.c_str(), "RESIZED %255s %u %u", shmName, &newCols, &newRows) == 3) {
                spdlog::info("RemoteTerminalBackend: server resized to {}x{}, remapping shm", newCols, newRows);
                shmName_ = shmName;
                
                // Remap shared memory
                sharedGrid_.reset();
                sharedGrid_.reset(SharedGrid::openClient(shmName_));
                if (!sharedGrid_ || !sharedGrid_->isValid()) {
                    spdlog::error("Failed to remap shared memory after resize");
                } else {
                    // Update SharedGridView
                    sharedGridView_ = std::make_unique<SharedGridView>(sharedGrid_.get());
                    sharedGridView_->setFont(font_);
                    cols_ = newCols;
                    rows_ = newRows;
                    fullDamage_ = true;
                    spdlog::info("RemoteTerminalBackend: remapped to {}x{}", newCols, newRows);
                }
            }
        }
    }
}

//=============================================================================
// libuv callbacks
//=============================================================================

void RemoteTerminalBackend::onConnect(uv_connect_t* req, int status) {
    auto* self = static_cast<RemoteTerminalBackend*>(req->data);
    delete req;
    
    if (status < 0) {
        spdlog::error("RemoteTerminalBackend: connect error: {}", uv_strerror(status));
        self->connected_ = false;
        return;
    }
    
    spdlog::info("RemoteTerminalBackend: connected to server");
    self->connected_ = true;
    
    // Start reading from server
    uv_read_start(reinterpret_cast<uv_stream_t*>(self->socket_), allocBuffer, onRead);
}

void RemoteTerminalBackend::allocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    auto* self = static_cast<RemoteTerminalBackend*>(handle->data);
    buf->base = self->readBuffer_.get();
    buf->len = READ_BUFFER_SIZE;
}

void RemoteTerminalBackend::onRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    auto* self = static_cast<RemoteTerminalBackend*>(stream->data);
    
    if (nread < 0) {
        if (nread != UV_EOF) {
            spdlog::error("RemoteTerminalBackend: read error: {}", uv_strerror(nread));
        }
        self->connected_ = false;
        self->running_ = false;
        return;
    }
    
    if (nread > 0) {
        self->onServerMessage(buf->base, nread);
    }
}

} // namespace yetty
