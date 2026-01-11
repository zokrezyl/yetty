"""
Yetty Python plugin initialization callbacks.

This module provides lifecycle hooks for the Python plugin system.
User scripts should not modify this file.
"""

import yetty_wgpu


def init_plugin():
    """
    Called once when the Python plugin is first loaded.
    """
    pass


def dispose_plugin():
    """
    Called when the Python plugin is unloaded.
    """
    pass


def init_widget(ctx, width, height):
    """
    Called when a new Python widget is created.

    Args:
        ctx: Dictionary with:
            - 'device': WGPUDevice handle
            - 'queue': WGPUQueue handle
            - 'widget_id': Unique widget ID
        width: Widget width in pixels
        height: Widget height in pixels

    Returns:
        widget_id for use in subsequent calls
    """
    widget_id = ctx.get('widget_id')
    print(f"[yetty] Initializing widget {widget_id}: {width}x{height}")

    # Set WebGPU handles (shared)
    yetty_wgpu.set_handles(
        device=ctx['device'],
        queue=ctx['queue']
    )

    # Create render texture for this widget
    if not yetty_wgpu.create_render_texture(widget_id, width, height):
        raise RuntimeError(f"Failed to create render texture for widget {widget_id}")

    # Set as current widget so user scripts can use init()/create_figure() without widget_id
    try:
        import yetty_pygfx
        yetty_pygfx.set_current_widget(widget_id)
    except ImportError:
        pass

    print(f"[yetty] Widget {widget_id} initialized: {width}x{height}")
    return widget_id


def dispose_widget(widget_id):
    """
    Called when a Python widget is destroyed.

    Args:
        widget_id: The widget ID to cleanup
    """
    print(f"[yetty] Disposing widget {widget_id}...")

    # Cleanup pygfx state for this widget
    try:
        import yetty_pygfx
        yetty_pygfx.cleanup(widget_id)
    except ImportError:
        pass
    except Exception as e:
        print(f"[yetty] Warning: pygfx cleanup failed: {e}")

    # Cleanup C++ texture (already done by yetty_pygfx.cleanup, but be safe)
    try:
        yetty_wgpu.cleanup_widget(widget_id)
    except:
        pass

    print(f"[yetty] Widget {widget_id} disposed")
