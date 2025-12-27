<p align="center">
  <img src="docs/logo.jpeg" alt="Yetty Logo" width="200">
</p>

# Yetty - Next-Generation Terminal Emulator

A WebGPU-powered terminal emulator that breaks the boundaries of what terminals can display.

## The Problem We're Solving

Traditional terminals are stuck in the 1970s. They can display text, maybe some colors, and that's about it. While the rest of computing has evolved to support rich graphics, animations, and interactive content, terminals remain fundamentally limited to a character grid.

**Yetty changes this.**

## What Makes Yetty Different

### Plugin-Based Rendering System

Yetty introduces a revolutionary plugin architecture that allows **anything** to be rendered within the terminal:

- **Images** - Display inline images that scroll with your content
- **Live Graphics** - Embed real-time GPU-accelerated visualizations
- **Shaders** - Run ShaderToy-style WGSL shaders directly in your terminal
- **Interactive Widgets** - ImGui-based UI elements (via ymery plugin)
- **Custom Content** - Write your own plugins to display anything

The only limit is your imagination.

### How It Works

Plugins occupy cells in the terminal grid. When the terminal scrolls, plugin content scrolls with it - just like text in a Jupyter notebook. This enables workflows that were never possible before:

```
$ python train_model.py
Training epoch 1/100...
[Live loss graph rendered here, scrolling with output]
Training epoch 2/100...
[Another visualization]
...
```

### GPU-Accelerated Everything

- **MSDF Font Rendering** - Crisp, scalable text at any zoom level
- **WebGPU Backend** - Modern, cross-platform GPU acceleration
- **60fps Animations** - Smooth shader animations and transitions
- **Runs Everywhere** - Native (Linux, macOS, Windows) and Web (WASM)

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     Terminal Grid                        │
│  ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐     │
│  │  H  │  e  │  l  │  l  │  o  │     │  W  │  o  │     │
│  ├─────┼─────┼─────┴─────┴─────┼─────┼─────┼─────┤     │
│  │  r  │  l  │ ╔═══════════╗   │  !  │     │     │     │
│  ├─────┼─────┤ ║  Plugin   ║   ├─────┼─────┼─────┤     │
│  │     │     │ ║  Layer    ║   │     │     │     │     │
│  ├─────┼─────┤ ║ (shader,  ║   ├─────┼─────┼─────┤     │
│  │     │     │ ║  image,   ║   │     │     │     │     │
│  ├─────┼─────┤ ║  widget)  ║   ├─────┼─────┼─────┤     │
│  │     │     │ ╚═══════════╝   │     │     │     │     │
│  └─────┴─────┴─────────────────┴─────┴─────┴─────┘     │
└─────────────────────────────────────────────────────────┘
```

### Rendering Pipeline

1. **Terminal Grid** - MSDF text rendering via WebGPU
2. **Custom Glyph Layers** - Animated glyphs (planned)
3. **Plugin Layers** - Images, shaders, widgets rendered as overlays

## Built-in Plugins

### ShaderToy (`shader`)

Run WGSL fragment shaders directly in your terminal. Supports:
- Time-based animation
- Mouse interaction
- Custom parameters via scroll wheel

### Image (`image`)

Display inline images with:
- Automatic sizing to cell grid
- Support for PNG, JPEG, etc.
- Scrolling with terminal content

### Ymery (`ymery`)

ImGui-based interactive widgets:
- Buttons, sliders, text inputs
- Custom UI layouts
- Full keyboard/mouse support

## Technical Highlights

- **libvterm** for VT100/xterm compatibility
- **fontconfig** for system font discovery and fallback
- **msdfgen** for multi-channel signed distance field font rendering
- **wgpu-native** / Dawn for WebGPU abstraction
- **GLFW** for windowing (native) or Emscripten (web)

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
./yetty
```

## Usage

```bash
# Run with default shell
./yetty

# Run with specific command
SHELL=/bin/zsh ./yetty

# Demo mode (scrolling text)
./yetty --demo
```

## Future Plans

- **Animated Glyphs** - Shader-based animated emoji and custom glyphs
- **Color Emoji** - Proper emoji rendering via bitmap fallback
- **Plugin SDK** - Easy development of custom plugins
- **Configuration** - User-configurable themes, fonts, keybindings

## License

[Add license here]

---

*Yetty: Your terminal, unchained.*
