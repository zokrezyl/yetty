#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <vterm.h>

namespace ymux
{

// Matches ygrid wire format GridCell (12 bytes)
struct GridCell {
    uint32_t codepoint;
    uint8_t fgR, fgG, fgB, alpha;
    uint8_t bgR, bgG, bgB, style;
};
static_assert(sizeof(GridCell) == 12, "GridCell must be 12 bytes");

// YGRD wire format constants
static constexpr uint32_t YGRD_MAGIC = 0x59475244;
static constexpr uint16_t YGRD_FLAG_FULL = 0x0001;

// Style attribute bits (match gpu-screen constants)
static constexpr uint8_t ATTR_BOLD = 0x01;
static constexpr uint8_t ATTR_ITALIC = 0x02;
static constexpr uint8_t ATTR_UNDERLINE_SHIFT = 2;
static constexpr uint8_t ATTR_STRIKETHROUGH = 0x10;

class Pane
{
  public:
    using Ptr = std::unique_ptr<Pane>;
    using DamageCallback = std::function<void(Pane &pane)>;

    static Ptr create(uint16_t cols, uint16_t rows, const std::string &shell = "");

    ~Pane();

    // PTY file descriptor for polling
    int fd() const
    {
        return _ptyFd;
    }

    // Feed data from PTY into vterm
    void feedInput(const char *data, size_t len);

    // Send keypress to child via PTY
    void sendKey(const char *data, size_t len);

    // Grid dimensions
    uint16_t cols() const
    {
        return _cols;
    }
    uint16_t rows() const
    {
        return _rows;
    }

    // Card name for OSC targeting
    const std::string &cardName() const
    {
        return _cardName;
    }
    void setCardName(const std::string &name)
    {
        _cardName = name;
    }

    // Set damage callback
    void setDamageCallback(DamageCallback cb)
    {
        _damageCb = std::move(cb);
    }

    // Build YGRD full update payload (all cells)
    std::vector<uint8_t> buildFullPayload() const;

    // Build YGRD partial update payload (only damaged cells since last call)
    std::vector<uint8_t> buildDamagePayload();

    // Check if there's pending damage
    bool hasDamage() const
    {
        return _hasDamage;
    }
    void clearDamage()
    {
        _hasDamage = false;
    }

    // Resize the pane
    void resize(uint16_t cols, uint16_t rows);

    // Is child process still alive?
    bool isAlive() const;

  private:
    Pane(int ptyFd, pid_t childPid, uint16_t cols, uint16_t rows);

    // Read a cell from vterm screen
    GridCell readVTermCell(int row, int col) const;

    // VTerm callbacks
    static int onDamage(VTermRect rect, void *user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);

    int _ptyFd = -1;
    pid_t _childPid = -1;
    uint16_t _cols, _rows;
    std::string _cardName;

    VTerm *_vt = nullptr;
    VTermScreen *_vtScreen = nullptr;

    // Damage tracking
    bool _hasDamage = false;
    std::vector<bool> _damagedCells; // cols*rows bitmap

    DamageCallback _damageCb;
};

} // namespace ymux
