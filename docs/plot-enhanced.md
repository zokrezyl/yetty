# Enhanced Plot Card: Expression-Based Plotting

## Overview

This document describes enhancements to the plot card to support mathematical expressions, animated plots with time-based uniforms, named buffer declarations, and external data streaming.

**Goals:**
- Support mathematical expressions like `sin(x)`, `x^2 + 2*x - 1`
- Allow animated plots using `time` uniform
- Named buffer declarations for external streaming clients
- Combine buffer data with expressions: `data(x) * sin(x)`
- Maintain backward compatibility with existing buffer-only plots
- Leverage ShaderManager for dynamic WGSL code generation

## Quick Reference

### Buffer Declaration Syntax

```
Left side (inputs):              Right side (outputs):
  f=buffer                         label=f(x)
  @f.size=400                      @label.color=#00FF00
  @f.values=

Full example:
{card=plot; audio=buffer; @audio.size=1k: waveform=audio(x); @waveform.color=#00FF00}
```

### Buffer Properties

| Property | Description |
|----------|-------------|
| `f=buffer` | Declare buffer-backed function |
| `@f.size=N` | Capacity in floats (required unless file) |
| `@f.values=` | Initialize from payload |
| `@f.values="file"` | Initialize from file (size deduced) |
| `@f.values=1,2,3` | Inline initialization |

## Current Architecture

### Data Flow (Existing)
```
yetty-client → OSC sequence → plot card → buffer upload → shader renders
                              (parse CSV)  (cardStorage)   (sample buffer)
```

### Shader Structure (Existing)
The plot shader (`0x0001-plot.wgsl`) currently:
1. Reads metadata (plot type, colors, data range, zoom/pan)
2. Reads data points from `cardStorage` buffer
3. Interpolates between points based on x position
4. Renders line/bar/scatter/area visualization

## Enhanced Architecture

### New Data Flow
```
yetty-client → OSC sequence → plot card → ShaderManager → GPU renders
               (expression)   (parse)     (inject WGSL)   (evaluate)
```

### Expression Compilation
Expressions are compiled to WGSL functions and injected into the plot shader:

```
Input:  "sin(x) + cos(2*x)"
Output: fn plot_expr_0(x: f32, time: f32, b: array<f32>) -> f32 {
            return sin(x) + cos(2.0 * x);
        }
```

## Syntax

### Domain and Viewport

| Syntax | Meaning | Example |
|--------|---------|---------|
| `x=A..B` | X domain (evaluation range) | `x=-pi..pi` |
| `y=A..B` | Y domain (for 2D heatmaps) | `y=-2..2` |
| `@view=X1..X2,Y1..Y2` | Initial viewport (zoomable) | `@view=-1..1,-0.5..1.5` |

### Buffer Declarations (Input Side)

Buffers are declared on the **left side** of `:` as named input functions:

```
{card=plot; f=buffer; @f.size=400: waveform=f(x)}
            ^^^^^^^^  ^^^^^^^^^^^  ^^^^^^^^^^^^^
            declare   properties   output (uses f)
```

| Syntax | Meaning |
|--------|---------|
| `f=buffer` | Declare `f` as a buffer-backed function |
| `@f.size=N` | Buffer capacity (N floats) |
| `@f.size=1k` | Size with suffix (1k=1024, 1m=1048576) |
| `@f.values=` | Initialize from payload (empty = payload) |
| `@f.values="file.csv"` | Initialize from file |
| `@f.values=1.0,2.0,3.0` | Inline initialization |

#### Size Resolution Rules

| Declaration | Size Source |
|-------------|-------------|
| `@f.size=400` | Explicit size |
| `@f.values="file"` | Deduced from file |
| `@f.values="file"; @f.size=100` | Explicit overrides (truncate/limit) |
| `@f.values=` (payload) | Requires `@f.size` |
| (streaming, no values) | Requires `@f.size` |

#### Payload Ordering

When multiple buffers use `@*.values=`, the declaration order defines payload layout:

```bash
{card=plot; bx=buffer; by=buffer; @bx.size=3; @by.size=3; @bx.values=; @by.values=: scatter(bx,by)}
1.0,2.0,3.0,4.0,5.0,6.0
```

Maps to:
- `bx = [1.0, 2.0, 3.0]` (first `@*.values=`)
- `by = [4.0, 5.0, 6.0]` (second `@*.values=`)

### Buffer References (Legacy)

For backward compatibility, anonymous buffer references are still supported:

| Syntax | Meaning |
|--------|---------|
| `@buffer1` | First data buffer (y-values) |
| `@buffer2` | Second data buffer |
| `@bufferN` | N-th data buffer |

Buffers are sampled by normalized x position: `buffer[x * length]` with interpolation.

### Built-in Uniforms

| Name | Type | Description |
|------|------|-------------|
| `x` | f32 | Domain variable (horizontal position) |
| `y` | f32 | Second domain variable (for 2D) |
| `time` | f32 | Elapsed time in seconds |
| `pi` | f32 | π constant (3.14159...) |
| `e` | f32 | Euler's number (2.71828...) |

### Built-in Functions

| Function | Description |
|----------|-------------|
| `sin(x)`, `cos(x)`, `tan(x)` | Trigonometric |
| `asin(x)`, `acos(x)`, `atan(x)` | Inverse trig |
| `sinh(x)`, `cosh(x)`, `tanh(x)` | Hyperbolic |
| `exp(x)`, `log(x)`, `log2(x)` | Exponential/logarithmic |
| `sqrt(x)`, `pow(x,y)`, `abs(x)` | Power/absolute |
| `floor(x)`, `ceil(x)`, `fract(x)` | Rounding |
| `min(a,b)`, `max(a,b)`, `clamp(x,a,b)` | Clamping |
| `sign(x)`, `step(e,x)`, `smoothstep(a,b,x)` | Step functions |

### Expression Grammar

```ebnf
expression  = term (('+' | '-') term)*
term        = factor (('*' | '/') factor)*
factor      = unary ('^' unary)?
unary       = '-'? primary
primary     = NUMBER
            | IDENTIFIER
            | IDENTIFIER '(' args ')'
            | '(' expression ')'
args        = expression (',' expression)*
IDENTIFIER  = [a-zA-Z_][a-zA-Z0-9_]*
            | '@buffer' [0-9]+
NUMBER      = [0-9]+ ('.' [0-9]+)?
```

## Examples

### Pure Function Plots

```bash
# Basic function
{plot x=-pi..pi: sin(x)}

# Multiple functions (separate series)
{plot x=-pi..pi: sin(x), cos(x), tan(x)}

# Polynomial
{plot x=-5..5: x^3 - 2*x^2 + x - 1}

# Composition
{plot x=-10..10: sin(x)/x}

# With viewport
{plot x=-10..10 @view=-5..5,-1..1: sin(x)/x}
```

### Animated Plots

```bash
# Traveling wave
{plot x=0..10: sin(x - time)}

# Amplitude modulation
{plot x=-pi..pi: sin(x) * cos(time)}

# Frequency sweep
{plot x=0..10: sin(x * (1 + time/10))}
```

### Buffer-Based Plots

#### Named Buffers (New Syntax)

```bash
# Single buffer with payload data
{card=plot; f=buffer; @f.size=100; @f.values=: wave=f(x)}
0.1,0.2,0.3,...

# Buffer from file (size deduced)
{card=plot; data=buffer; @data.values="signal.csv": amplitude=data(x)}

# Buffer from file with size limit
{card=plot; data=buffer; @data.values="signal.csv"; @data.size=256: amplitude=data(x)}

# Two buffers for scatter plot
{card=plot; bx=buffer; by=buffer; @bx.size=100; @by.size=100; @bx.values=; @by.values=: points=scatter(bx,by)}
# payload: x0,x1,...,x99,y0,y1,...,y99

# Streaming buffer (pre-allocated, no initial values)
{card=plot; audio=buffer; @audio.size=1k: signal=audio(x)}

# Transform buffer values
{card=plot; raw=buffer; @raw.size=256; @raw.values=: normalized=raw(x)*2-1}

# Buffer combined with function
{card=plot; data=buffer; @data.size=100; @data.values=: modulated=data(x)*sin(x*10)}

# Animated buffer
{card=plot; data=buffer; @data.size=100; @data.values=: pulsing=data(x)*(1+0.5*sin(time))}

# Buffer + computed function overlaid
{card=plot; measured=buffer; @measured.size=100; @measured.values=: raw=measured(x); @raw.color=#FF0000; ideal=sin(x); @ideal.color=#00FF00}
```

#### Anonymous Buffers (Legacy Syntax)

```bash
# Direct buffer plot (existing behavior)
{plot: @buffer1}

# Multiple buffers
{plot: @buffer1, @buffer2}

# Transform buffer values
{plot: log(@buffer1)}
{plot: @buffer1 * 2 + 1}

# Buffer combined with function
{plot: @buffer1 * sin(x * 10)}

# Animated buffer
{plot: @buffer1 * (1 + 0.5 * sin(time))}
```

### External Streaming

Named buffers enable external clients to stream data to plot cards via shared memory.

#### Workflow

1. **Create card with pre-allocated buffer:**
   ```bash
   {card=plot; audio=buffer; @audio.size=400: waveform=audio(x)}
   ```

2. **Client connects via RPC:**
   ```cpp
   client.call("stream_connect");  // Get shared memory info
   ```

3. **Client queries buffer location:**
   ```cpp
   client.call("stream_get_buffer", {
       {"name", "my-plot-card"},   // Card name
       {"scope", "audio"}          // Buffer name
   });
   // Returns: {offset: 1024, size: 1600}
   ```

4. **Client writes directly to shared memory:**
   ```cpp
   float* data = (float*)(shm_base + offset);
   for (int i = 0; i < 400; i++) {
       data[i] = generate_sample(i);
   }
   ```

#### Example: Audio Visualization

```bash
# Terminal command - create plot with streaming buffer
yecho "{card=plot; w=80; h=20; audio=buffer; @audio.size=1024; x=0..1: waveform=audio(x); @waveform.color=#00FF00}"
```

```cpp
// External client
void stream_audio() {
    auto shm = open_shared_memory(shm_name);
    auto buf = rpc.call("stream_get_buffer", {{"name", card_name}, {"scope", "audio"}});

    float* samples = (float*)((char*)shm->data() + buf.offset);

    while (running) {
        capture_audio(samples, 1024);
        std::this_thread::sleep_for(16ms);  // ~60 fps
    }
}
```

#### Flag-Based Syntax

For OSC/CLI, the equivalent syntax:

```bash
# Create streaming buffer
printf '\033]666666;run -c plot -w 80 -h 20 --name my-plot;--buffer audio=1024\033\\'

# With file initialization
printf '\033]666666;run -c plot;--buffer data="initial.csv"\033\\'

# Multiple buffers
printf '\033]666666;run -c plot;--buffer x=100,y=100\033\\'
```

### 2D Heatmaps (Future)

```bash
# Function of two variables
{plot x=-2..2 y=-2..2 mode=heatmap: sin(x) * cos(y)}

# Interference pattern
{plot x=-5..5 y=-5..5 mode=heatmap: sin(sqrt(x^2 + y^2))}
```

## Implementation Plan

### Phase 1: Expression Parser (C++)

Create `ExpressionParser` class in plot card:

```cpp
class ExpressionParser {
public:
    struct ParseResult {
        std::string wgslCode;           // Generated WGSL function body
        std::vector<int> bufferRefs;    // Which @bufferN are referenced
        bool usesTime;                  // References time uniform
        bool usesY;                     // References y (2D mode)
    };
    
    Result<ParseResult> parse(const std::string& expr);
    
private:
    // Recursive descent parser
    Result<std::string> parseExpression();
    Result<std::string> parseTerm();
    Result<std::string> parseFactor();
    Result<std::string> parseUnary();
    Result<std::string> parsePrimary();
};
```

### Phase 2: Metadata Extension

Add expression mode to plot metadata:

```cpp
// Extended metadata (64 bytes, same size)
struct Metadata {
    // Existing fields...
    uint32_t plotType_flags;     // [0] Add EXPR flag (bit 4)
    uint32_t widthHeight;        // [1]
    uint32_t dataOffset;         // [2] Buffer offset (if used)
    uint32_t dataCount;          // [3] Buffer length (if used)
    float minValue;              // [4] 
    float maxValue;              // [5]
    // ...
    
    // New: domain specification
    float domainXMin;            // [13] x=A..B → A
    float domainXMax;            // [14] x=A..B → B
    uint32_t exprIndex;          // [15] Index of injected expression function
};
```

### Phase 3: Shader Injection

Extend ShaderManager to support plot expression injection:

```cpp
// In ShaderManager
Result<uint32_t> injectPlotExpression(const std::string& wgslBody);

// Generated WGSL
fn plot_expr_0(x: f32, time: f32, buffers: ptr<storage, array<f32>>, buf_offset: u32, buf_len: u32) -> f32 {
    // Injected expression body
    return sin(x) + cos(2.0 * x);
}
```

### Phase 4: Shader Modification

Modify `0x0001-plot.wgsl` to call expression functions:

```wgsl
// Expression evaluation dispatcher
fn evaluateExpression(exprIndex: u32, x: f32, time: f32, ...) -> f32 {
    switch (exprIndex) {
        case 0u: { return plot_expr_0(x, time, ...); }
        case 1u: { return plot_expr_1(x, time, ...); }
        // ...
        default: { return 0.0; }
    }
}

// In main rendering loop
if ((flags & FLAG_EXPR) != 0u) {
    // Expression mode: evaluate function
    let y = evaluateExpression(exprIndex, dataX, time, ...);
} else {
    // Buffer mode: sample from cardStorage (existing behavior)
    let y = sampleBuffer(dataOffset, dataCount, dataX);
}
```

### Phase 5: OSC Protocol Extension

Extend OSC plugin args:

```
# Existing
--type line --grid --line-color 0xff0000

# New expression options
--expr "sin(x)"           # Expression to evaluate
--domain -pi,pi           # X domain (or parsed from x=A..B)
--view -1,1,-2,2          # Viewport (x1,x2,y1,y2)
--animated                # Enable time uniform updates

# New buffer declaration options
--buffer name=size        # Declare named buffer: --buffer audio=1024
--buffer name=size,...    # Multiple buffers: --buffer x=100,y=100
--buffer name="file"      # From file (size deduced): --buffer data="signal.csv"
--buffer.name.prop=val    # Buffer properties: --buffer.audio.values=
```

#### Buffer Declaration Examples

```bash
# Pre-allocated for streaming
--buffer audio=1024

# Multiple buffers
--buffer x=100,y=100

# From file
--buffer data="waveform.csv"

# From file with size limit
--buffer data="waveform.csv" --buffer.data.size=256

# From payload (requires explicit size)
--buffer signal=512 --buffer.signal.values=
```

### Phase 6: yecho Integration

Update yecho parser to recognize plot blocks:

```cpp
// In YEchoParser
if (cardName == "plot") {
    // Parse: {plot x=-pi..pi @view=-1..1,-1..1: sin(x)}
    auto domain = parseRange("x");
    auto viewport = parseRange("@view");
    auto expr = content;  // After ':'
    
    // Emit OSC with expression
    emitPlotOSC(domain, viewport, expr);
}
```

## Buffer Sampling

When expressions reference `@bufferN`, the WGSL samples from cardStorage:

```wgsl
fn sampleBuffer(offset: u32, count: u32, x: f32) -> f32 {
    // x is normalized [0, 1] over domain
    let idx = x * f32(count - 1u);
    let i0 = u32(floor(idx));
    let i1 = min(i0 + 1u, count - 1u);
    let t = fract(idx);
    
    let v0 = cardStorage[offset + i0];
    let v1 = cardStorage[offset + i1];
    
    return mix(v0, v1, t);  // Linear interpolation
}
```

## Backward Compatibility

- Plots without `--expr` work exactly as before (buffer mode)
- Default behavior unchanged: CSV payload → buffer → render
- Expression mode is opt-in via `--expr` or yecho `{plot: expr}` syntax

## Performance Considerations

1. **Expression compilation**: One-time cost when plot is created
2. **Shader recompilation**: Only when new unique expressions are added
3. **Animation**: Time uniform updated per frame (existing mechanism)
4. **Buffer sampling**: Texture-based sampling for better cache performance (future optimization)

## Future Extensions

1. **2D heatmaps**: `{plot x=-2..2 y=-2..2 mode=heatmap: f(x,y)}`
2. **Parametric curves**: `{plot parametric t=0..2*pi: cos(t), sin(t)}`
3. **Vector fields**: `{plot x=-2..2 y=-2..2 mode=vector: -y, x}`
4. **Custom colormaps**: `--colormap viridis|plasma|magma`
5. **Multiple y-axes**: Different scales for different series
6. **Buffer element types**: `@f.type=f32|i32|u8` for different data formats
7. **Ring buffers**: `@f.mode=ring` for continuous streaming with wrap-around
