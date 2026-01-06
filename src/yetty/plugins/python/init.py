"""
Yetty Python plugin initialization callbacks.

This module provides lifecycle hooks for the Python plugin system.
User scripts should not modify this file.
"""

import yetty_wgpu

# Global state
_plugin_initialized = False


def init_plugin():
    """
    Called once when the Python plugin is loaded.
    
    This is the place for global initialization that should happen
    once per plugin, not per layer.
    """
    global _plugin_initialized
    
    if _plugin_initialized:
        return
    
    print("[yetty] Python plugin initializing...")
    
    # Any global setup goes here
    
    _plugin_initialized = True
    print("[yetty] Python plugin initialized")


def init_layer(ctx, width, height):
    """
    Called when a new Python layer is created.
    
    This is called BEFORE the user script runs, so WebGPU resources
    are available to the user script.
    
    Args:
        ctx: Dictionary with WebGPU context info:
            - 'device': WGPUDevice handle (as int)
            - 'queue': WGPUQueue handle (as int)
            - 'width': Render target width
            - 'height': Render target height
        width: Layer width in pixels
        height: Layer height in pixels
    """
    print(f"[yetty] Initializing layer: {width}x{height}")
    
    # Set WebGPU handles for yetty_wgpu module
    yetty_wgpu.set_handles(
        device=ctx['device'],
        queue=ctx['queue']
    )
    
    # Create render texture for this layer
    if not yetty_wgpu.create_render_texture(width, height):
        raise RuntimeError(f"Failed to create render texture {width}x{height}")
    
    print(f"[yetty] Layer initialized: {width}x{height}")


def dispose_layer():
    """
    Called when a Python layer is destroyed.
    
    Clean up any layer-specific resources here.
    """
    print("[yetty] Disposing layer...")
    
    # Cleanup yetty_wgpu resources
    yetty_wgpu.cleanup()
    
    print("[yetty] Layer disposed")


def dispose_plugin():
    """
    Called when the Python plugin is unloaded (future feature).
    
    This is a placeholder for future implementation.
    """
    global _plugin_initialized
    
    print("[yetty] Python plugin disposing...")
    
    # Global cleanup goes here
    
    _plugin_initialized = False
    print("[yetty] Python plugin disposed")
