// Shader glyph: YPaint widget (codepoint 1048580 / U+100004)
// GPU-accelerated SDF primitives with O(1) UNIFORM GRID SPATIAL HASHING
// + MSDF text rendering
//
// Uses shared SDF functions from lib/distfunctions.wgsl
// Uses text rendering from lib/text.wgsl
//
// =============================================================================
// Metadata layout (64 bytes = 16 u32s):
//   offset 0:  primitiveOffset (u32)
//   offset 1:  primitiveCount (u32)
//   offset 2:  gridOffset (u32)
//   offset 3:  gridWidth (u32)
//   offset 4:  gridHeight (u32)
//   offset 5:  cellSizeXY (cellSizeX f16 low | cellSizeY f16 high)
//   offset 6:  glyphOffset (u32)   - offset of glyph data in cardStorage
//   offset 7:  glyphCount (u32)    - number of text glyphs
//   offset 8:  sceneMinX (f32)     - content bounds (= grid origin)
//   offset 9:  sceneMinY (f32)
//   offset 10: sceneMaxX (f32)
//   offset 11: sceneMaxY (f32)
//   offset 12: widthCells  [15:0] = widthCells,  [31:16] = panX (i16 fixedpoint)
//   offset 13: heightCells [15:0] = heightCells, [31:16] = panY (i16 fixedpoint)
//   offset 14: flags       [15:0] = flags,       [31:16] = zoom (f16)
//   offset 15: bgColor (u32)
//
// Pan fixedpoint: panScene = i16value / 16384.0 * sceneExtent
// Zoom f16: IEEE 754 half-float
// =============================================================================
// YPaintGlyph layout (20 bytes = 5 u32s):
//   offset 0: x (f32)        - position in scene coords
//   offset 1: y (f32)
//   offset 2: width (f16 low) | height (f16 high)
//   offset 3: glyphIndex (u16 low) | layer (u8) | flags (u8)
//   offset 4: color (u32)    - packed RGBA
// =============================================================================
// When FLAG_CUSTOM_ATLAS is set, after the glyph array in cardStorage:
//   [atlasHeader: 4 u32s] [GlyphMetadataGPU: 10 floats each]
//   atlasHeader[0] = (atlasX & 0xFFFF) | (msdfAtlasW << 16)
//   atlasHeader[1] = (atlasY & 0xFFFF) | (msdfAtlasH << 16)
//   atlasHeader[2] = glyphMetaCount
//   atlasHeader[3] = reserved
// GlyphMetadataGPU: uvMinX, uvMinY, uvMaxX, uvMaxY, sizeX, sizeY, bearingX, bearingY, advance, pad
// =============================================================================

const YPAINT_FLAG_SHOW_BOUNDS: u32 = 1u;
const YPAINT_FLAG_SHOW_GRID: u32 = 2u;
const YPAINT_FLAG_SHOW_EVAL_COUNT: u32 = 4u;
const YPAINT_FLAG_HAS_3D: u32 = 8u;
const YPAINT_FLAG_UNIFORM_SCALE: u32 = 16u;
const YPAINT_FLAG_CUSTOM_ATLAS: u32 = 32u;

// Per-glyph flag bits (in flags byte = bits 24-31 of glyphLayerFlags)
const YPAINT_GLYPH_FLAG_CUSTOM_ATLAS: u32 = 1u;
const YPAINT_GLYPH_FLAG_SELECTED: u32 = 2u;
const YPAINT_SELECTION_COLOR: vec3<f32> = vec3<f32>(0.2, 0.56, 1.0);  // #3390FF

// Safety cap for entries per cell (variable-length grid stores actual count)
const YPAINT_MAX_ENTRIES_PER_CELL: u32 = 4096u;

// Bit 31 set = glyph index, clear = primitive index
const YPAINT_GLYPH_BIT: u32 = 0x80000000u;
const YPAINT_INDEX_MASK: u32 = 0x7FFFFFFFu;

// =============================================================================
// Color Wheel Support
// =============================================================================

fn ypaint_hsv2rgb(hsv: vec3<f32>) -> vec3<f32> {
    let h = hsv.x * 6.0;
    let s = hsv.y;
    let v = hsv.z;
    let i = floor(h);
    let f = h - i;
    let p = v * (1.0 - s);
    let q = v * (1.0 - s * f);
    let t = v * (1.0 - s * (1.0 - f));
    let ii = i32(i) % 6;
    if (ii == 0) { return vec3<f32>(v, t, p); }
    if (ii == 1) { return vec3<f32>(q, v, p); }
    if (ii == 2) { return vec3<f32>(p, v, t); }
    if (ii == 3) { return vec3<f32>(p, q, v); }
    if (ii == 4) { return vec3<f32>(t, p, v); }
    return vec3<f32>(v, p, q);
}

fn ypaint_renderColorWheel(p: vec2<f32>, center: vec2<f32>, outerR: f32, innerR: f32,
                    hue: f32, sat: f32, val: f32, indicatorSize: f32) -> vec4<f32> {
    let d = p - center;
    let dist = length(d);
    let angle = atan2(d.y, d.x);
    let normAngle = (angle + 3.14159265) / (2.0 * 3.14159265);
    let aa = 1.5;
    let ringWidth = outerR - innerR;

    // Hue ring
    if (dist >= innerR && dist <= outerR) {
        let color = ypaint_hsv2rgb(vec3<f32>(normAngle, 1.0, 1.0));
        let ringDist = min(dist - innerR, outerR - dist);
        let alpha = clamp(ringDist / aa + 0.5, 0.0, 1.0);

        // Hue indicator
        let hueAngle = hue * 2.0 * 3.14159265 - 3.14159265;
        let indPos = center + vec2<f32>(cos(hueAngle), sin(hueAngle)) * (innerR + ringWidth * 0.5);
        let indDist = length(p - indPos);
        if (indDist < indicatorSize) {
            if (indDist < indicatorSize - 2.0) { return vec4<f32>(1.0, 1.0, 1.0, 1.0); }
            return vec4<f32>(0.0, 0.0, 0.0, 1.0);
        }
        return vec4<f32>(color, alpha);
    }

    // SV triangle
    let triR = innerR * 0.85;
    let hueAng = hue * 2.0 * 3.14159265;
    let v0 = center + vec2<f32>(cos(hueAng), sin(hueAng)) * triR;
    let v1 = center + vec2<f32>(cos(hueAng + 2.094395), sin(hueAng + 2.094395)) * triR;
    let v2 = center + vec2<f32>(cos(hueAng + 4.18879), sin(hueAng + 4.18879)) * triR;

    let d0 = v1 - v0; let d1 = v2 - v0; let d2 = p - v0;
    let dot00 = dot(d0, d0); let dot01 = dot(d0, d1);
    let dot02 = dot(d0, d2); let dot11 = dot(d1, d1); let dot12 = dot(d1, d2);
    let invD = 1.0 / (dot00 * dot11 - dot01 * dot01);
    let u = (dot11 * dot02 - dot01 * dot12) * invD;
    let v = (dot00 * dot12 - dot01 * dot02) * invD;

    if (u >= 0.0 && v >= 0.0 && u + v <= 1.0) {
        let svS = clamp(1.0 - v - u * 0.5, 0.0, 1.0);
        let svV = clamp(1.0 - v, 0.0, 1.0);
        let color = ypaint_hsv2rgb(vec3<f32>(hue, svS, svV));
        let edge = min(u, min(v, 1.0 - u - v)) * triR * 2.0;
        let alpha = clamp(edge / aa + 0.5, 0.0, 1.0);

        // SV indicator
        let selU = (1.0 - val) * 2.0 / 3.0 + val * (1.0 - sat) / 3.0;
        let selV = 1.0 - val;
        let selPos = v0 + d0 * selU + d1 * selV;
        let selDist = length(p - selPos);
        if (selDist < indicatorSize) {
            if (selDist < indicatorSize - 2.0) { return vec4<f32>(1.0, 1.0, 1.0, 1.0); }
            return vec4<f32>(0.0, 0.0, 0.0, 1.0);
        }
        return vec4<f32>(color, alpha);
    }

    return vec4<f32>(0.0, 0.0, 0.0, 0.0);
}

// =============================================================================
// Main shader function - O(1) GRID LOOKUP
// =============================================================================

fn shaderGlyph_1048580(localUV: vec2<f32>, time: f32, fg: u32, bg: u32, pixelPos: vec2<f32>, mousePos: vec2<f32>, lastChar: u32, lastCharTime: f32) -> vec3<f32> {
    // Decode ANSI-compatible cell encoding
    let slotIndex = fg & 0xFFFFFFu;
    let metaOffset = slotIndex * 16u;

    let bg24 = bg & 0xFFFFFFu;
    let relCol = bg24 & 0xFFFu;
    let relRow = (bg24 >> 12u) & 0xFFFu;

    // Read metadata (minimize reads - only what we need)
    let primitiveOffset = cardMetadata[metaOffset + 0u];
    let gridOffset = cardMetadata[metaOffset + 2u];
    let gridWidth = cardMetadata[metaOffset + 3u];
    let gridHeight = cardMetadata[metaOffset + 4u];
    // Unpack f16 pair: cellSizeX (low 16 bits), cellSizeY (high 16 bits)
    let cellSizeXY = unpack2x16float(cardMetadata[metaOffset + 5u]);
    let cellSizeX = cellSizeXY.x;
    let cellSizeY = cellSizeXY.y;
    let glyphOffset = cardMetadata[metaOffset + 6u];
    let glyphCount = cardMetadata[metaOffset + 7u];
    let invCellSizeX = select(0.0, 1.0 / cellSizeX, cellSizeX > 0.0);
    let invCellSizeY = select(0.0, 1.0 / cellSizeY, cellSizeY > 0.0);

    // Content bounds (= grid origin, never changes with zoom/pan)
    let contentMinX = bitcast<f32>(cardMetadata[metaOffset + 8u]);
    let contentMinY = bitcast<f32>(cardMetadata[metaOffset + 9u]);
    let contentMaxX = bitcast<f32>(cardMetadata[metaOffset + 10u]);
    let contentMaxY = bitcast<f32>(cardMetadata[metaOffset + 11u]);

    // Unpack widthCells/heightCells (lower 16 bits) and pan (upper 16 bits)
    let packedWC = cardMetadata[metaOffset + 12u];
    let packedHC = cardMetadata[metaOffset + 13u];
    let widthCells = packedWC & 0xFFFFu;
    let heightCells = packedHC & 0xFFFFu;
    let panXi16 = i32(packedWC >> 16u) - select(0, 65536, (packedWC >> 16u) >= 32768u);
    let panYi16 = i32(packedHC >> 16u) - select(0, 65536, (packedHC >> 16u) >= 32768u);

    // Unpack flags (lower 16 bits) and zoom f16 (upper 16 bits)
    let packedFlags = cardMetadata[metaOffset + 14u];
    let flags = packedFlags & 0xFFFFu;
    let zoomF16 = packedFlags >> 16u;

    // f16 → f32 conversion
    let f16sign = (zoomF16 >> 15u) & 1u;
    let f16exp  = (zoomF16 >> 10u) & 0x1Fu;
    let f16mant = zoomF16 & 0x3FFu;
    let f32bits = select(
        (f16sign << 31u) | ((f16exp + 112u) << 23u) | (f16mant << 13u),
        0u,
        f16exp == 0u
    );
    let zoom = select(bitcast<f32>(f32bits), 1.0, f16exp == 0u);

    let bgColorPacked = cardMetadata[metaOffset + 15u];
    let bgColor = unpackColor(bgColorPacked);

    // Content extent
    let contentW = contentMaxX - contentMinX;
    let contentH = contentMaxY - contentMinY;

    // Reconstruct pan in scene units: panScene = i16value / 16384.0 * extent
    let panX = f32(panXi16) / 16384.0 * contentW;
    let panY = f32(panYi16) / 16384.0 * contentH;

    // Compute view bounds: zoom around content center, then apply pan
    let centerX = (contentMinX + contentMaxX) * 0.5;
    let centerY = (contentMinY + contentMaxY) * 0.5;
    let viewHalfW = contentW * 0.5 / zoom;
    let viewHalfH = contentH * 0.5 / zoom;
    let viewMinX = centerX - viewHalfW + panX;
    let viewMinY = centerY - viewHalfH + panY;
    let viewMaxX = centerX + viewHalfW + panX;
    let viewMaxY = centerY + viewHalfH + panY;
    let viewW = viewMaxX - viewMinX;
    let viewH = viewMaxY - viewMinY;

    // Compute widget UV (position within entire card widget)
    let widgetUV = (vec2<f32>(f32(relCol), f32(relRow)) + localUV) /
                   vec2<f32>(f32(widthCells), f32(heightCells));

    // Compute scene-space position
    var scenePos: vec2<f32>;
    if ((flags & YPAINT_FLAG_UNIFORM_SCALE) != 0u) {
        // Uniform scaling: preserves aspect ratio (circles stay circular)
        let cardAspect = (f32(widthCells) * grid.cellSize.x) / max(f32(heightCells) * grid.cellSize.y, 1.0);
        let viewAspect = viewW / max(viewH, 1e-6);
        if (viewAspect < cardAspect) {
            let visibleW = viewH * cardAspect;
            let offsetX = (visibleW - viewW) * 0.5;
            scenePos = vec2<f32>(
                viewMinX - offsetX + widgetUV.x * visibleW,
                viewMinY + widgetUV.y * viewH
            );
        } else {
            let visibleH = viewW / cardAspect;
            let offsetY = (visibleH - viewH) * 0.5;
            scenePos = vec2<f32>(
                viewMinX + widgetUV.x * viewW,
                viewMinY - offsetY + widgetUV.y * visibleH
            );
        }
    } else {
        // Non-uniform: stretch to fill
        scenePos = vec2<f32>(
            viewMinX + widgetUV.x * viewW,
            viewMinY + widgetUV.y * viewH
        );
    }

    var resultColor = bgColor;
    var evalCount = 0u;

    // Compact primitive layout: offset table + variable-length prim data
    // primitiveOffset → [wordOff_0]...[wordOff_{N-1}] → [prim0_data][prim1_data]...
    let primitiveCount = cardMetadata[metaOffset + 1u];
    let primDataBase = primitiveOffset + primitiveCount;

    // 3D raymarching pass (before 2D grid lookup)
    if ((flags & YPAINT_FLAG_HAS_3D) != 0u) {
        // Camera setup using widget UV
        let aspect3D = (f32(widthCells) * grid.cellSize.x) / max(f32(heightCells) * grid.cellSize.y, 1.0);
        let uv3D = (widgetUV - 0.5) * vec2<f32>(aspect3D, 1.0);
        let camPos = vec3<f32>(0.0, 0.0, 3.0);
        let camDir = normalize(vec3<f32>(uv3D.x, -uv3D.y, -1.5));

        // Raymarching loop
        var t = 0.0;
        var hit = false;
        var hitPos = camPos;

        for (var step = 0; step < 64; step++) {
            hitPos = camPos + camDir * t;
            var minD = 1e10;
            for (var pi = 0u; pi < primitiveCount; pi++) {
                let pOff = primDataBase + bitcast<u32>(cardStorage[primitiveOffset + pi]);
                let pType = bitcast<u32>(cardStorage[pOff]);
                if (pType >= 100u) {
                    let d = evaluateYpaintSDF3D(pOff, hitPos);
                    minD = min(minD, d);
                }
            }
            if (minD < 0.001) { hit = true; break; }
            if (t > 100.0) { break; }
            t += minD;
        }

        if (hit) {
            // Calculate normal via finite differences (6 scene SDF evaluations)
            let e = vec2<f32>(0.001, 0.0);
            var sdfPX = 1e10; var sdfNX = 1e10;
            var sdfPY = 1e10; var sdfNY = 1e10;
            var sdfPZ = 1e10; var sdfNZ = 1e10;
            var hitColor = vec3<f32>(0.8, 0.8, 0.8);
            var hitMinD = 1e10;
            for (var pi = 0u; pi < primitiveCount; pi++) {
                let pOff = primDataBase + bitcast<u32>(cardStorage[primitiveOffset + pi]);
                let pType = bitcast<u32>(cardStorage[pOff]);
                if (pType >= 100u) {
                    sdfPX = min(sdfPX, evaluateYpaintSDF3D(pOff, hitPos + vec3<f32>(e.x, e.y, e.y)));
                    sdfNX = min(sdfNX, evaluateYpaintSDF3D(pOff, hitPos - vec3<f32>(e.x, e.y, e.y)));
                    sdfPY = min(sdfPY, evaluateYpaintSDF3D(pOff, hitPos + vec3<f32>(e.y, e.x, e.y)));
                    sdfNY = min(sdfNY, evaluateYpaintSDF3D(pOff, hitPos - vec3<f32>(e.y, e.x, e.y)));
                    sdfPZ = min(sdfPZ, evaluateYpaintSDF3D(pOff, hitPos + vec3<f32>(e.y, e.y, e.x)));
                    sdfNZ = min(sdfNZ, evaluateYpaintSDF3D(pOff, hitPos - vec3<f32>(e.y, e.y, e.x)));
                    // Find closest primitive for material color
                    let d = evaluateYpaintSDF3D(pOff, hitPos);
                    if (d < hitMinD) {
                        hitMinD = d;
                        let colors = ypaintPrimColors(pOff);
                        if (colors.x != 0u) {
                            let primType = bitcast<u32>(cardStorage[pOff + 0u]);
                            if (ypaintIsGradientPrim(primType)) {
                                hitColor = ypaintEvalGradientFillColor(pOff, hitPos.xy).rgb;
                            } else {
                                hitColor = unpackColor(colors.x).rgb;
                            }
                        }
                    }
                }
            }
            let n = normalize(vec3<f32>(sdfPX - sdfNX, sdfPY - sdfNY, sdfPZ - sdfNZ));

            // Lighting: diffuse + ambient
            let lightDir = normalize(vec3<f32>(1.0, 1.0, 1.0));
            let diff = max(dot(n, lightDir), 0.0);
            let amb = 0.2;
            resultColor = hitColor * (amb + diff * 0.8);
        }
    }

    // Early exit if no 2D grid
    if (gridWidth == 0u || gridHeight == 0u) {
        return resultColor;
    }

    // Compute screen scale for MSDF anti-aliasing (use view width, not content width)
    let pixelWidth = f32(widthCells) * grid.cellSize.x;
    let screenScale = select(1.0, pixelWidth / viewW, viewW > 0.0);

    // Read custom atlas header if flag is set
    let hasCustomAtlas = (flags & YPAINT_FLAG_CUSTOM_ATLAS) != 0u;
    var atlasX = 0u; var atlasY = 0u;
    var msdfAtlasW = 0u; var msdfAtlasH = 0u;
    var glyphMetaOff = 0u;
    if (hasCustomAtlas) {
        let atlasHeaderOff = glyphOffset + glyphCount * 5u;
        let atlasXW = bitcast<u32>(cardStorage[atlasHeaderOff]);
        let atlasYH = bitcast<u32>(cardStorage[atlasHeaderOff + 1u]);
        atlasX = atlasXW & 0xFFFFu;
        atlasY = atlasYH & 0xFFFFu;
        msdfAtlasW = atlasXW >> 16u;
        msdfAtlasH = atlasYH >> 16u;
        glyphMetaOff = atlasHeaderOff + 4u;
    }

    // O(1) GRID LOOKUP - use CONTENT bounds (grid origin), not view bounds
    let cellX = u32(clamp((scenePos.x - contentMinX) * invCellSizeX, 0.0, f32(gridWidth - 1u)));
    let cellY = u32(clamp((scenePos.y - contentMinY) * invCellSizeY, 0.0, f32(gridHeight - 1u)));
    let cellIndex = cellY * gridWidth + cellX;

    // Variable-length grid: [off0][off1]...[offN] [count,entries...] ...
    // offsets[cellIndex] points to packed [count][idx0][idx1]... for that cell
    let packedStart = bitcast<u32>(cardStorage[gridOffset + cellIndex]);
    let cellEntryCount = bitcast<u32>(cardStorage[gridOffset + packedStart]);
    let loopCount = min(cellEntryCount, YPAINT_MAX_ENTRIES_PER_CELL);

    let glyphSize = 5u;

    for (var i = 0u; i < loopCount; i++) {
        let rawIdx = bitcast<u32>(cardStorage[gridOffset + packedStart + 1u + i]);

        if ((rawIdx & YPAINT_GLYPH_BIT) != 0u) {
            // ---- TEXT GLYPH (MSDF) ----
            let gi = rawIdx & YPAINT_INDEX_MASK;
            let gOffset = glyphOffset + gi * glyphSize;

            let gx = cardStorage[gOffset + 0u];
            let gy = cardStorage[gOffset + 1u];
            // Decode f16 width/height from packed u32
            let whPacked = bitcast<u32>(cardStorage[gOffset + 2u]);
            let gw = unpack2x16float(whPacked).x;
            let gh = unpack2x16float(whPacked).y;
            // Decode glyphIndex (u16), layer (u8), flags (u8)
            let glfPacked = bitcast<u32>(cardStorage[gOffset + 3u]);
            let gIdx = glfPacked & 0xFFFFu;
            let gFlags = (glfPacked >> 24u) & 0xFFu;
            let gColorPacked = bitcast<u32>(cardStorage[gOffset + 4u]);

            if (scenePos.x >= gx && scenePos.x < gx + gw &&
                scenePos.y >= gy && scenePos.y < gy + gh) {

                // Selection highlight (drawn behind glyph)
                if ((gFlags & YPAINT_GLYPH_FLAG_SELECTED) != 0u) {
                    resultColor = mix(resultColor, YPAINT_SELECTION_COLOR, 0.3);
                }

                let glyphUV = vec2<f32>(
                    (scenePos.x - gx) / gw,
                    (scenePos.y - gy) / gh
                );
                let gColor = unpackColor(gColorPacked);

                if ((gFlags & YPAINT_GLYPH_FLAG_CUSTOM_ATLAS) != 0u && hasCustomAtlas) {
                    // Custom atlas glyph — read UV from local glyph metadata in cardStorage
                    let metaOff = glyphMetaOff + gIdx * 10u;
                    let uvMinX = cardStorage[metaOff + 0u];
                    let uvMinY = cardStorage[metaOff + 1u];
                    let uvMaxX = cardStorage[metaOff + 2u];
                    let uvMaxY = cardStorage[metaOff + 3u];

                    let msdfUV = vec2<f32>(
                        mix(uvMinX, uvMaxX, glyphUV.x),
                        mix(uvMinY, uvMaxY, glyphUV.y)
                    );
                    let msdfPixel = msdfUV * vec2<f32>(f32(msdfAtlasW), f32(msdfAtlasH));
                    let cardPixel = vec2<f32>(f32(atlasX), f32(atlasY)) + msdfPixel;
                    let sampleUV = cardPixel / vec2<f32>(textureDimensions(cardImageAtlas));
                    let msdf = textureSampleLevel(cardImageAtlas, cardImageSampler, sampleUV, 0.0);

                    let sd = median(msdf.r, msdf.g, msdf.b);
                    let pxDist = grid.pixelRange * (sd - 0.5);
                    let alpha = clamp(pxDist * screenScale + 0.5, 0.0, 1.0);
                    if (alpha > 0.01) {
                        resultColor = mix(resultColor, gColor, alpha);
                    }
                } else {
                    // Shared atlas glyph — use existing renderMsdfGlyph
                    let glyphResult = renderMsdfGlyph(glyphUV, gIdx, gColor, resultColor, grid.pixelRange, screenScale);
                    if (glyphResult.alpha > 0.01) {
                        resultColor = glyphResult.color;
                    }
                }
            }
        } else {
            // ---- SDF PRIMITIVE (compact variable-length layout) ----
            // Grid entries store word offsets relative to primDataBase (translated by CPU)
            let primOff = primDataBase + rawIdx;

            evalCount++;

            // Check for special primitives (ColorWheel) that return color directly
            // Layout: [0]=gridOffset, [1]=type, [2]=layer, [3+]=geometry
            let primType = bitcast<u32>(cardStorage[primOff + 1u]);
            if (primType == SDF_COLOR_WHEEL) {
                // Compact ColorWheel: cx=3, cy=4, outerR=5, innerR=6, hue=7, sat=8, val=9, indicatorSize=10
                let cwCenter = vec2<f32>(cardStorage[primOff + 3u], cardStorage[primOff + 4u]);
                let cwOuterR = cardStorage[primOff + 5u];
                let cwInnerR = cardStorage[primOff + 6u];
                let cwHue = cardStorage[primOff + 7u];
                let cwSat = cardStorage[primOff + 8u];
                let cwVal = cardStorage[primOff + 9u];
                let cwIndicator = cardStorage[primOff + 10u];

                let cwColor = ypaint_renderColorWheel(scenePos, cwCenter, cwOuterR, cwInnerR,
                                               cwHue, cwSat, cwVal, cwIndicator);
                if (cwColor.a > 0.01) {
                    resultColor = mix(resultColor, cwColor.rgb, cwColor.a);
                }
                continue;
            }

            if (primType == SDF_IMAGE) {
                // ---- IMAGE PRIMITIVE (type 129) ----
                // x=3, y=4, w=5, h=6, atlasX=7, atlasY=8, texW=9, texH=10
                let imgX = cardStorage[primOff + 3u];
                let imgY = cardStorage[primOff + 4u];
                let imgW = cardStorage[primOff + 5u];
                let imgH = cardStorage[primOff + 6u];
                let imgAtlasX = bitcast<u32>(cardStorage[primOff + 7u]);
                let imgAtlasY = bitcast<u32>(cardStorage[primOff + 8u]);
                let imgTexW = bitcast<u32>(cardStorage[primOff + 9u]);
                let imgTexH = bitcast<u32>(cardStorage[primOff + 10u]);

                // Check if inside image bounds
                if (scenePos.x >= imgX && scenePos.x < imgX + imgW &&
                    scenePos.y >= imgY && scenePos.y < imgY + imgH) {
                    // Compute UV within image (0-1)
                    let imgUV = vec2<f32>(
                        (scenePos.x - imgX) / imgW,
                        (scenePos.y - imgY) / imgH
                    );
                    // Map to atlas pixel coordinates
                    let atlasPixel = vec2<f32>(
                        f32(imgAtlasX) + imgUV.x * f32(imgTexW),
                        f32(imgAtlasY) + imgUV.y * f32(imgTexH)
                    );
                    // Sample from atlas
                    let atlasDims = vec2<f32>(textureDimensions(cardImageAtlas));
                    let sampleUV = atlasPixel / atlasDims;
                    let imgColor = textureSampleLevel(cardImageAtlas, cardImageSampler, sampleUV, 0.0);
                    if (imgColor.a > 0.01) {
                        resultColor = mix(resultColor, imgColor.rgb, imgColor.a);
                    }
                }
                continue;
            }

            if (primType == 65u && hasCustomAtlas) {
                // ---- ROTATED MSDF GLYPH (type 65, compact layout) ----
                // x=3, y=4, scaleX=5, scaleY=6, angle=7, glyphIndex=8, cosAngle=9, sinAngle=10, fillColor=11
                let gx = cardStorage[primOff + 3u];
                let gy = cardStorage[primOff + 4u];
                let gw = cardStorage[primOff + 5u];
                let gh = cardStorage[primOff + 6u];
                let gIdx = bitcast<u32>(cardStorage[primOff + 8u]);
                let gColor = unpackColor(bitcast<u32>(cardStorage[primOff + 11u]));

                // Use pre-computed cos/sin from compact layout
                let ca = cardStorage[primOff + 9u];   // cosAngle
                let sa = cardStorage[primOff + 10u];  // sinAngle

                // Inverse-rotate scene point into glyph-local space
                let dx = scenePos.x - gx;
                let dy = scenePos.y - gy;
                let lx = dx * ca + dy * sa;
                let ly = -dx * sa + dy * ca;

                if (lx >= 0.0 && lx < gw && ly >= 0.0 && ly < gh) {
                    let glyphUV = vec2<f32>(lx / gw, ly / gh);
                    let metaOff = glyphMetaOff + gIdx * 10u;
                    let uvMinX = cardStorage[metaOff + 0u];
                    let uvMinY = cardStorage[metaOff + 1u];
                    let uvMaxX = cardStorage[metaOff + 2u];
                    let uvMaxY = cardStorage[metaOff + 3u];
                    let msdfUV = vec2<f32>(
                        mix(uvMinX, uvMaxX, glyphUV.x),
                        mix(uvMinY, uvMaxY, glyphUV.y)
                    );
                    let msdfPixel = msdfUV * vec2<f32>(f32(msdfAtlasW), f32(msdfAtlasH));
                    let cardPixel = vec2<f32>(f32(atlasX), f32(atlasY)) + msdfPixel;
                    let sampleUV = cardPixel / vec2<f32>(textureDimensions(cardImageAtlas));
                    let msdf = textureSampleLevel(cardImageAtlas, cardImageSampler, sampleUV, 0.0);
                    let sd = median(msdf.r, msdf.g, msdf.b);
                    let pxDist = grid.pixelRange * (sd - 0.5);
                    let alpha = clamp(pxDist * screenScale + 0.5, 0.0, 1.0);
                    if (alpha > 0.01) {
                        resultColor = mix(resultColor, gColor, alpha);
                    }
                }
            } else {
                // ---- REGULAR SDF PRIMITIVE (compact layout) ----
                let d = evaluateYpaintSDF(primOff, scenePos);

                let colors = ypaintPrimColors(primOff);
                let fillColorPacked = colors.x;
                if (d < 0.0 && fillColorPacked != 0u) {
                    let primType = bitcast<u32>(cardStorage[primOff + 1u]);
                    var fillColorAlpha: vec4<f32>;
                    if (ypaintIsGradientPrim(primType)) {
                        fillColorAlpha = ypaintEvalGradientFillColor(primOff, scenePos);
                    } else {
                        fillColorAlpha = unpackColorAlpha(fillColorPacked);
                    }
                    let edgeAlpha = clamp(-d * 2.0, 0.0, 1.0);
                    let alpha = edgeAlpha * fillColorAlpha.a;
                    resultColor = mix(resultColor, fillColorAlpha.rgb, alpha);
                }

                let strokeColorPacked = colors.y;
                let strokeWidth = ypaintPrimStrokeWidth(primOff);
                if (strokeWidth > 0.0 && strokeColorPacked != 0u) {
                    let strokeDist = abs(d) - strokeWidth * 0.5;
                    if (strokeDist < 0.0) {
                        let strokeColorAlpha = unpackColorAlpha(strokeColorPacked);
                        let edgeAlpha = clamp(-strokeDist * 2.0, 0.0, 1.0);
                        let alpha = edgeAlpha * strokeColorAlpha.a;
                        resultColor = mix(resultColor, strokeColorAlpha.rgb, alpha);
                    }
                }
            }
        }
    }

    // Debug: show grid lines (use floor instead of expensive modulo)
    if ((flags & YPAINT_FLAG_SHOW_GRID) != 0u) {
        let cellPosX = (scenePos.x - contentMinX) * invCellSizeX;
        let cellPosY = (scenePos.y - contentMinY) * invCellSizeY;
        let fracX = fract(cellPosX);
        let fracY = fract(cellPosY);
        let gridLineWidth = 0.02;
        if (fracX < gridLineWidth || fracY < gridLineWidth) {
            resultColor = mix(resultColor, vec3<f32>(0.5, 0.5, 0.5), 0.5);
        }
    }

    // Debug: show evaluation count heatmap
    if ((flags & YPAINT_FLAG_SHOW_EVAL_COUNT) != 0u) {
        let t = clamp(f32(evalCount) / 4.0, 0.0, 1.0);
        return vec3<f32>(t, 1.0 - t * 0.5, 0.0);
    }

    return resultColor;
}
