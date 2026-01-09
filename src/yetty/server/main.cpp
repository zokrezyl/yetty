//=============================================================================
// yetty-server - Terminal multiplexer server
//
// Runs VTerm + PTY and exposes Grid via shared memory.
// Clients connect via Unix socket for input and receive damage notifications.
//=============================================================================

#include "../local-terminal-backend.h"
#include "../shared-grid.h"
#include "../terminal-backend.h"

#include <spdlog/spdlog.h>
#include <uv.h>

#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

// Server state
struct ServerState {
    uv_loop_t* loop = nullptr;
    uv_pipe_t* server = nullptr;
    uv_timer_t* syncTimer = nullptr;
    
    std::shared_ptr<yetty::LocalTerminalBackend> backend;
    std::unique_ptr<yetty::SharedGrid> sharedGrid;
    
    std::vector<uv_pipe_t*> clients;
    
    std::string socketPath;
    std::string shmName;
    
    uint32_t cols = 80;
    uint32_t rows = 24;
    
    bool running = true;
};

ServerState g_state;

// Forward declarations
void onNewConnection(uv_stream_t* server, int status);
void onClientRead(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
void allocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
void onSyncTimer(uv_timer_t* handle);
void broadcastDamage();
void handleClientCommand(uv_pipe_t* client, const std::string& cmd);

//=============================================================================
// Client connection handling
//=============================================================================

void onNewConnection(uv_stream_t* server, int status) {
    if (status < 0) {
        spdlog::error("Connection error: {}", uv_strerror(status));
        return;
    }

    auto* client = new uv_pipe_t;
    uv_pipe_init(g_state.loop, client, 0);
    
    if (uv_accept(server, reinterpret_cast<uv_stream_t*>(client)) == 0) {
        client->data = nullptr;
        g_state.clients.push_back(client);
        uv_read_start(reinterpret_cast<uv_stream_t*>(client), allocBuffer, onClientRead);
        spdlog::info("Client connected ({} total)", g_state.clients.size());
        
        // Send initial state
        std::string msg = "CONNECTED " + g_state.shmName + " " +
                          std::to_string(g_state.cols) + " " +
                          std::to_string(g_state.rows) + "\n";
        uv_buf_t buf = uv_buf_init(const_cast<char*>(msg.c_str()), msg.size());
        uv_write_t* req = new uv_write_t;
        uv_write(req, reinterpret_cast<uv_stream_t*>(client), &buf, 1,
                 [](uv_write_t* req, int) { delete req; });
    } else {
        uv_close(reinterpret_cast<uv_handle_t*>(client),
                 [](uv_handle_t* h) { delete reinterpret_cast<uv_pipe_t*>(h); });
    }
}

void allocBuffer(uv_handle_t*, size_t suggested_size, uv_buf_t* buf) {
    buf->base = new char[suggested_size];
    buf->len = suggested_size;
}

void onClientRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    auto* client = reinterpret_cast<uv_pipe_t*>(stream);
    
    if (nread < 0) {
        if (nread != UV_EOF) {
            spdlog::error("Read error: {}", uv_strerror(nread));
        }
        // Remove client
        auto it = std::find(g_state.clients.begin(), g_state.clients.end(), client);
        if (it != g_state.clients.end()) {
            g_state.clients.erase(it);
        }
        uv_close(reinterpret_cast<uv_handle_t*>(client),
                 [](uv_handle_t* h) { delete reinterpret_cast<uv_pipe_t*>(h); });
        spdlog::info("Client disconnected ({} remaining)", g_state.clients.size());
        delete[] buf->base;
        return;
    }

    if (nread > 0) {
        std::string cmd(buf->base, nread);
        handleClientCommand(client, cmd);
    }
    
    delete[] buf->base;
}

void handleClientCommand(uv_pipe_t* client, const std::string& cmd) {
    // Parse command (newline-separated)
    size_t pos = 0;
    while (pos < cmd.size()) {
        size_t end = cmd.find('\n', pos);
        if (end == std::string::npos) end = cmd.size();
        
        std::string line = cmd.substr(pos, end - pos);
        pos = end + 1;
        
        if (line.empty()) continue;
        
        spdlog::debug("Command from client: {}", line);
        
        // Parse command
        if (line.rfind("KEY ", 0) == 0) {
            // KEY <codepoint> <mod>
            uint32_t codepoint;
            int mod;
            if (sscanf(line.c_str(), "KEY %u %d", &codepoint, &mod) == 2) {
                g_state.backend->sendKey(codepoint, static_cast<VTermModifier>(mod));
            }
        } else if (line.rfind("SPECIAL ", 0) == 0) {
            // SPECIAL <key> <mod>
            int key, mod;
            if (sscanf(line.c_str(), "SPECIAL %d %d", &key, &mod) == 2) {
                g_state.backend->sendSpecialKey(static_cast<VTermKey>(key),
                                                 static_cast<VTermModifier>(mod));
            }
        } else if (line.rfind("RAW ", 0) == 0) {
            // RAW <len>\n<data>
            size_t len;
            if (sscanf(line.c_str(), "RAW %zu", &len) == 1) {
                // Next bytes are raw data
                if (pos + len <= cmd.size()) {
                    g_state.backend->sendRaw(cmd.c_str() + pos, len);
                    pos += len;
                }
            }
        } else if (line.rfind("RESIZE ", 0) == 0) {
            // RESIZE <cols> <rows>
            uint32_t cols, rows;
            if (sscanf(line.c_str(), "RESIZE %u %u", &cols, &rows) == 2) {
                if (cols != g_state.cols || rows != g_state.rows) {
                    g_state.cols = cols;
                    g_state.rows = rows;
                    g_state.backend->resize(cols, rows);
                    
                    // Recreate shared grid with new size
                    g_state.sharedGrid.reset();
                    yetty::SharedGrid::unlink(g_state.shmName);
                    g_state.sharedGrid.reset(yetty::SharedGrid::createServer(g_state.shmName, cols, rows));
                    if (!g_state.sharedGrid || !g_state.sharedGrid->isValid()) {
                        spdlog::error("Failed to recreate shared grid for resize");
                    } else {
                        spdlog::info("Resized to {}x{}", cols, rows);
                        // Notify all clients to remap
                        std::string msg = "RESIZED " + g_state.shmName + " " + 
                                          std::to_string(cols) + " " + std::to_string(rows) + "\n";
                        for (auto* c : g_state.clients) {
                            uv_buf_t buf = uv_buf_init(const_cast<char*>(msg.c_str()), msg.size());
                            uv_write_t* wreq = new uv_write_t;
                            uv_write(wreq, reinterpret_cast<uv_stream_t*>(c), &buf, 1, 
                                     [](uv_write_t* r, int) { delete r; });
                        }
                    }
                }
            }
        } else if (line.rfind("SCROLL ", 0) == 0) {
            int lines;
            if (sscanf(line.c_str(), "SCROLL %d", &lines) == 1) {
                if (lines > 0) {
                    g_state.backend->scrollUp(lines);
                } else {
                    g_state.backend->scrollDown(-lines);
                }
            }
        } else if (line == "SCROLL_TOP") {
            g_state.backend->scrollToTop();
        } else if (line == "SCROLL_BOTTOM") {
            g_state.backend->scrollToBottom();
        } else if (line.rfind("START", 0) == 0) {
            // START [shell] - already started, just acknowledge
            std::string msg = "OK\n";
            uv_buf_t buf = uv_buf_init(const_cast<char*>(msg.c_str()), msg.size());
            uv_write_t* req = new uv_write_t;
            uv_write(req, reinterpret_cast<uv_stream_t*>(client), &buf, 1,
                     [](uv_write_t* req, int) { delete req; });
        }
    }
}

//=============================================================================
// Grid sync and damage broadcast
//=============================================================================

void onSyncTimer(uv_timer_t*) {
    if (!g_state.backend || !g_state.sharedGrid) return;
    
    // Sync backend grid to shared memory (double-buffered)
    if (g_state.backend->hasDamage()) {
        g_state.backend->syncToGrid();
        
        // Copy to back buffer
        g_state.sharedGrid->copyFromGrid(g_state.backend->getGrid());
        
        // Update back buffer header with cursor and damage info
        auto& rects = g_state.backend->getDamageRects();
        uint32_t dsr = 0, dsc = 0, der = g_state.rows, dec = g_state.cols;
        if (!rects.empty()) {
            dsr = rects[0]._startRow;
            dsc = rects[0]._startCol;
            der = rects[0]._endRow;
            dec = rects[0]._endCol;
            for (size_t i = 1; i < rects.size(); i++) {
                dsr = std::min(dsr, rects[i]._startRow);
                dsc = std::min(dsc, rects[i]._startCol);
                der = std::max(der, rects[i]._endRow);
                dec = std::max(dec, rects[i]._endCol);
            }
        }
        
        g_state.sharedGrid->updateBackBuffer(
            g_state.backend->getCursorRow(),
            g_state.backend->getCursorCol(),
            g_state.backend->isCursorVisible(),
            g_state.backend->isAltScreen(),
            g_state.backend->hasFullDamage(),
            dsr, dsc, der, dec,
            g_state.backend->getScrollOffset()
        );
        
        // Swap buffers atomically - client will now see new data
        g_state.sharedGrid->swapBuffers();
        
        g_state.backend->clearDamageRects();
        g_state.backend->clearFullDamage();
        
        // Broadcast damage to clients
        broadcastDamage();
    }
}

void broadcastDamage() {
    if (g_state.clients.empty()) return;
    
    // Get active buffer header (the one we just swapped to)
    auto* bufHdr = g_state.sharedGrid->activeBufferHeader();
    
    // Format: DAMAGE <seq> <fullDamage> <startRow> <startCol> <endRow> <endCol> <cursorRow> <cursorCol> <cursorVisible>\n
    char msg[256];
    snprintf(msg, sizeof(msg), "DAMAGE %u %d %u %u %u %u %d %d %d\n",
             bufHdr->sequenceNumber,
             bufHdr->fullDamage,
             bufHdr->damageStartRow, bufHdr->damageStartCol,
             bufHdr->damageEndRow, bufHdr->damageEndCol,
             bufHdr->cursorRow, bufHdr->cursorCol, bufHdr->cursorVisible);
    
    size_t len = strlen(msg);
    
    for (auto* client : g_state.clients) {
        char* data = new char[len];
        memcpy(data, msg, len);
        
        uv_buf_t buf = uv_buf_init(data, len);
        uv_write_t* req = new uv_write_t;
        req->data = data;
        uv_write(req, reinterpret_cast<uv_stream_t*>(client), &buf, 1,
                 [](uv_write_t* req, int) {
                     delete[] static_cast<char*>(req->data);
                     delete req;
                 });
    }
}

//=============================================================================
// Signal handling
//=============================================================================

void onSignal(uv_signal_t*, int) {
    spdlog::info("Received signal, shutting down...");
    g_state.running = false;
    uv_stop(g_state.loop);
}

//=============================================================================
// Main
//=============================================================================

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -s, --socket PATH   Socket path (default: $XDG_RUNTIME_DIR/yetty-server.sock)\n"
              << "  -m, --shm NAME      Shared memory name (default: /yetty-grid-0)\n"
              << "  -c, --cols N        Columns (default: 80)\n"
              << "  -r, --rows N        Rows (default: 24)\n"
              << "  -e, --exec CMD      Execute command instead of shell\n"
              << "  -h, --help          Show this help\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("yetty-server starting...");

    // Default paths
    const char* xdgRuntime = getenv("XDG_RUNTIME_DIR");
    if (xdgRuntime) {
        g_state.socketPath = std::string(xdgRuntime) + "/yetty-server.sock";
    } else {
        g_state.socketPath = "/tmp/yetty-server.sock";
    }
    g_state.shmName = "/yetty-grid-0";
    
    std::string shell;

    // Parse args
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if ((arg == "-s" || arg == "--socket") && i + 1 < argc) {
            g_state.socketPath = argv[++i];
        } else if ((arg == "-m" || arg == "--shm") && i + 1 < argc) {
            g_state.shmName = argv[++i];
        } else if ((arg == "-c" || arg == "--cols") && i + 1 < argc) {
            g_state.cols = std::stoul(argv[++i]);
        } else if ((arg == "-r" || arg == "--rows") && i + 1 < argc) {
            g_state.rows = std::stoul(argv[++i]);
        } else if ((arg == "-e" || arg == "--exec") && i + 1 < argc) {
            shell = argv[++i];
        }
    }

    // Create libuv loop
    g_state.loop = uv_default_loop();

    // Create shared grid
    g_state.sharedGrid.reset(yetty::SharedGrid::createServer(g_state.shmName, g_state.cols, g_state.rows));
    if (!g_state.sharedGrid || !g_state.sharedGrid->isValid()) {
        spdlog::error("Failed to create shared grid");
        return 1;
    }

    // Create terminal backend (without font - glyph indices will be codepoints)
    auto backendResult = yetty::LocalTerminalBackend::create(g_state.cols, g_state.rows, nullptr, g_state.loop);
    if (!backendResult) {
        spdlog::error("Failed to create terminal backend: {}", backendResult.error().message());
        return 1;
    }
    g_state.backend = std::move(*backendResult);

    // Start shell
    if (auto res = g_state.backend->start(shell); !res) {
        spdlog::error("Failed to start shell: {}", res.error().message());
        return 1;
    }

    // Remove old socket
    unlink(g_state.socketPath.c_str());

    // Create server socket
    g_state.server = new uv_pipe_t;
    uv_pipe_init(g_state.loop, g_state.server, 0);
    
    int r = uv_pipe_bind(g_state.server, g_state.socketPath.c_str());
    if (r < 0) {
        spdlog::error("Bind error: {}", uv_strerror(r));
        return 1;
    }
    
    r = uv_listen(reinterpret_cast<uv_stream_t*>(g_state.server), 128, onNewConnection);
    if (r < 0) {
        spdlog::error("Listen error: {}", uv_strerror(r));
        return 1;
    }

    // Make socket accessible
    chmod(g_state.socketPath.c_str(), 0666);

    // Create sync timer (50Hz - sync grid to shared memory)
    g_state.syncTimer = new uv_timer_t;
    uv_timer_init(g_state.loop, g_state.syncTimer);
    uv_timer_start(g_state.syncTimer, onSyncTimer, 20, 20);

    // Signal handlers
    uv_signal_t sigint, sigterm;
    uv_signal_init(g_state.loop, &sigint);
    uv_signal_init(g_state.loop, &sigterm);
    uv_signal_start(&sigint, onSignal, SIGINT);
    uv_signal_start(&sigterm, onSignal, SIGTERM);

    spdlog::info("Server listening on {} (shm: {})", g_state.socketPath, g_state.shmName);
    spdlog::info("Grid: {}x{}", g_state.cols, g_state.rows);

    // Run event loop
    uv_run(g_state.loop, UV_RUN_DEFAULT);

    // Cleanup
    spdlog::info("Shutting down...");
    
    uv_timer_stop(g_state.syncTimer);
    uv_close(reinterpret_cast<uv_handle_t*>(g_state.syncTimer),
             [](uv_handle_t* h) { delete reinterpret_cast<uv_timer_t*>(h); });
    
    uv_close(reinterpret_cast<uv_handle_t*>(g_state.server),
             [](uv_handle_t* h) { delete reinterpret_cast<uv_pipe_t*>(h); });
    
    for (auto* client : g_state.clients) {
        uv_close(reinterpret_cast<uv_handle_t*>(client),
                 [](uv_handle_t* h) { delete reinterpret_cast<uv_pipe_t*>(h); });
    }
    
    uv_signal_stop(&sigint);
    uv_signal_stop(&sigterm);
    
    // Run loop once more to process close callbacks
    uv_run(g_state.loop, UV_RUN_NOWAIT);
    
    g_state.backend->stop();
    g_state.backend.reset();
    g_state.sharedGrid.reset();
    
    unlink(g_state.socketPath.c_str());
    
    uv_loop_close(g_state.loop);
    
    spdlog::info("Server stopped");
    return 0;
}
