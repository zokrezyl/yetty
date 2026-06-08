#pragma once

#include "pane.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ymux
{

class Multiplexer
{
  public:
    Multiplexer(uint16_t cols, uint16_t rows);
    ~Multiplexer();

    // Create a new pane and its ygrid card
    bool addPane(const std::string &shell = "");

    // Run the main event loop
    int run();

    // Stop the event loop
    void stop()
    {
        _running = false;
    }

    // Build OSC sequence to create an ygrid card (returns string, does NOT write)
    static std::string buildCreateOsc(const Pane &pane, int x, int y, int w, int h);

    // Build OSC update with YGRD payload (returns string, does NOT write)
    static std::string buildUpdateOsc(const Pane &pane, const std::vector<uint8_t> &payload);

    // Base64 encode
    static std::string base64Encode(const std::vector<uint8_t> &data);

  private:
    // Send OSC sequence to create an ygrid card
    void sendCreateCard(Pane &pane, int x, int y, int w, int h);

    // Send OSC update with YGRD payload
    void sendUpdate(Pane &pane, const std::vector<uint8_t> &payload);

    // Process input from stdin (handle meta keys for switching panes)
    void processStdinInput(const char *data, size_t len);

    uint16_t _cols, _rows;
    std::vector<Pane::Ptr> _panes;
    size_t _activePane = 0;
    bool _running = false;
    bool _escapeMode = false; // After prefix key
};

} // namespace ymux
