// =============================================================================
// YDraw Overlay Rendering
//
// Full-screen painter layers for YDraw content.
// Renders ydraw content from a metadata slot as a full-screen overlay.
// Returns vec4: rgb = color, a = coverage (0 = transparent, 1 = opaque)
// =============================================================================

fn renderYdrawOverlay(slotIndex: u32, pixelPos: vec2<f32>) -> vec4<f32> {
    if (slotIndex == 0u) {
        return vec4<f32>(0.0);  // Disabled
    }

    let metaOffset = slotIndex * 16u;
    let primitiveCount = cardMetadata[metaOffset + 1u];
    if (primitiveCount == 0u) {
        return vec4<f32>(0.0);  // No content
    }

    // Read metadata
    let primitiveOffset = cardMetadata[metaOffset + 0u];
    let gridOffset = cardMetadata[metaOffset + 2u];
    let gridWidth = cardMetadata[metaOffset + 3u];
    let gridHeight = cardMetadata[metaOffset + 4u];
    let cellSizeXY = unpack2x16float(cardMetadata[metaOffset + 5u]);
    let cellSizeX = cellSizeXY.x;
    let cellSizeY = cellSizeXY.y;
    let glyphOffset = cardMetadata[metaOffset + 6u];
    let glyphCount = cardMetadata[metaOffset + 7u];

    // Content bounds
    let contentMinX = bitcast<f32>(cardMetadata[metaOffset + 8u]);
    let contentMinY = bitcast<f32>(cardMetadata[metaOffset + 9u]);
    let contentMaxX = bitcast<f32>(cardMetadata[metaOffset + 10u]);
    let contentMaxY = bitcast<f32>(cardMetadata[metaOffset + 11u]);

    // Early exit if no grid
    if (gridWidth == 0u || gridHeight == 0u || cellSizeX <= 0.0 || cellSizeY <= 0.0) {
        return vec4<f32>(0.0);
    }

    // For full-screen overlay, map pixel position directly to scene coordinates
    // The scene bounds define where content is placed
    let contentW = contentMaxX - contentMinX;
    let contentH = contentMaxY - contentMinY;

    // Scale pixel position to scene - overlay covers full grid area
    let gridPixelW = grid.gridSize.x * grid.cellSize.x;
    let gridPixelH = grid.gridSize.y * grid.cellSize.y;
    let scenePos = vec2<f32>(
        contentMinX + (pixelPos.x / gridPixelW) * contentW,
        contentMinY + (pixelPos.y / gridPixelH) * contentH
    );

    // SDF distances below are in scene units, but the analytic AA ramp must
    // be one *pixel* wide. WGSL forbids fwidth() inside the non-uniform
    // primitive loop, so derive the scale from the same mapping as scenePos;
    // with anisotropic scale the smaller axis keeps the ramp >= 1px.
    let pixelsPerSceneX = gridPixelW / max(contentW, 1e-6);
    let pixelsPerSceneY = gridPixelH / max(contentH, 1e-6);
    let pixelsPerScene = min(pixelsPerSceneX, pixelsPerSceneY);
    let sceneUnitsPerPixel = 1.0 / max(pixelsPerScene, 1e-6);

    // Grid lookup
    let invCellSizeX = 1.0 / cellSizeX;
    let invCellSizeY = 1.0 / cellSizeY;
    let cellX = u32(clamp((scenePos.x - contentMinX) * invCellSizeX, 0.0, f32(gridWidth - 1u)));
    let cellY = u32(clamp((scenePos.y - contentMinY) * invCellSizeY, 0.0, f32(gridHeight - 1u)));
    let cellIndex = cellY * gridWidth + cellX;

    // Variable-length grid lookup
    let packedStart = bitcast<u32>(cardStorage[gridOffset + cellIndex]);
    let cellEntryCount = bitcast<u32>(cardStorage[gridOffset + packedStart]);
    let loopCount = min(cellEntryCount, 64u);  // Safety cap

    let primDataBase = primitiveOffset + primitiveCount;
    var resultColor = vec3<f32>(0.0);
    var resultAlpha = 0.0;

    for (var i = 0u; i < loopCount; i++) {
        let rawIdx = bitcast<u32>(cardStorage[gridOffset + packedStart + 1u + i]);

        // Skip glyphs for now (bit 31 set = glyph)
        if ((rawIdx & 0x80000000u) != 0u) {
            continue;
        }

        // SDF primitive
        let primOff = primDataBase + rawIdx;
        let d = evaluateYdrawSDF(primOff, scenePos);

        let colors = ydrawPrimColors(primOff);
        let fillColorPacked = colors.x;
        // One-pixel coverage ramp, half a pixel to each side of the boundary.
        // Coverage is gamma-corrected (^(1/2.2)) because compositing happens
        // on sRGB-encoded values; keeps thin light-on-dark fringes at their
        // linear-light brightness.
        if (d * pixelsPerScene < 0.5 && fillColorPacked != 0u) {
            let fillColorAlpha = unpackColorAlpha(fillColorPacked);
            let coverage = clamp(0.5 - d * pixelsPerScene, 0.0, 1.0);
            let alpha = pow(coverage, 1.0 / 2.2) * fillColorAlpha.a;
            resultColor = mix(resultColor, fillColorAlpha.rgb, alpha);
            resultAlpha = max(resultAlpha, alpha);
        }

        // Strokes thinner than 1px draw as a 1px band dimmed by the
        // requested width, so hairlines stay visible (and uniform) at any
        // subpixel position.
        let strokeColorPacked = colors.y;
        let strokeWidth = ydrawPrimStrokeWidth(primOff);
        if (strokeWidth > 0.0 && strokeColorPacked != 0u) {
            let effectiveStrokeWidth = max(strokeWidth, sceneUnitsPerPixel);
            let strokeDist = abs(d) - effectiveStrokeWidth * 0.5;
            if (strokeDist * pixelsPerScene < 0.5) {
                let strokeColorAlpha = unpackColorAlpha(strokeColorPacked);
                let coverage = clamp(0.5 - strokeDist * pixelsPerScene, 0.0, 1.0) *
                               min(strokeWidth * pixelsPerScene, 1.0);
                let alpha = pow(coverage, 1.0 / 2.2) * strokeColorAlpha.a;
                resultColor = mix(resultColor, strokeColorAlpha.rgb, alpha);
                resultAlpha = max(resultAlpha, alpha);
            }
        }
    }

    return vec4<f32>(resultColor, resultAlpha);
}
