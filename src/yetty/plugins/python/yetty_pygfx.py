"""
yetty_pygfx - Direct integration of pygfx/fastplotlib with yetty's WebGPU context.

This module hooks into pygfx to use yetty's WebGPU device and render to
yetty's textures (no offscreen rendering or pixel copying).

Supports multiple simultaneous widgets - each gets its own render texture.
The adapter/device are shared (yetty's WebGPU context).

Must be imported BEFORE any pygfx/wgpu imports to work correctly.
"""

import os
import sys
import atexit

# Step 1: Point wgpu-py to yetty's wgpu-native library
_yetty_wgpu_lib = os.environ.get('YETTY_WGPU_LIB_PATH')
if _yetty_wgpu_lib:
    os.environ['WGPU_LIB_PATH'] = _yetty_wgpu_lib

# Force offscreen mode to avoid glfw issues
os.environ['RENDERCANVAS_FORCE_OFFSCREEN'] = '1'

# Import yetty_wgpu (our C++ module)
import yetty_wgpu

# Import wgpu
import wgpu
from wgpu.backends.wgpu_native import ffi, GPUDevice, GPUQueue, GPUAdapter, GPUAdapterInfo
from wgpu.backends.wgpu_native._api import GPUTexture, _get_limits


#-----------------------------------------------------------------------------
# Adapter/Device wrappers (shared across all widgets)
#-----------------------------------------------------------------------------

def _create_adapter_info():
    return GPUAdapterInfo({
        'vendor': 'yetty',
        'architecture': '',
        'device': 'yetty-embedded',
        'description': 'Yetty embedded WebGPU device',
        'backend': 'Vulkan',
        'adapter_type': 'DiscreteGPU',
        'vendor_id': 0,
        'device_id': 0,
    })


class YettyGPUAdapter(GPUAdapter):
    """Wrapper adapter that returns yetty's device."""

    def __init__(self, device_handle, queue_handle):
        self._device_handle = device_handle
        self._queue_handle = queue_handle
        self._internal = None
        self._features = frozenset(['float32-filterable'])
        self._limits = {}
        self._info = _create_adapter_info()

    @property
    def info(self):
        return self._info

    @property
    def features(self):
        return self._features

    @property
    def limits(self):
        return self._limits

    def request_device_sync(self, **kwargs):
        return _create_device(self._device_handle, self._queue_handle, self)


def _create_device(device_handle, queue_handle, adapter):
    device_ptr = ffi.cast('WGPUDevice', device_handle)
    queue_ptr = ffi.cast('WGPUQueue', queue_handle)

    features = set(adapter.features)
    try:
        limits = _get_limits(device_ptr, device=True)
    except:
        limits = {}

    queue = GPUQueue("yetty-queue", queue_ptr, None)
    device = GPUDevice("yetty-device", device_ptr, adapter, features, limits, queue)

    # Don't release - yetty owns these
    device._release_function = None
    queue._release_function = None

    return device


#-----------------------------------------------------------------------------
# Shared state (adapter/device - same for all widgets)
#-----------------------------------------------------------------------------

_adapter = None
_device = None
_initialized = False


def _init_shared():
    """Initialize shared adapter/device once."""
    global _adapter, _device, _initialized

    if _initialized:
        return

    if not yetty_wgpu.is_initialized():
        raise RuntimeError("yetty_wgpu not initialized")

    device_handle = yetty_wgpu.get_device_handle()
    queue_handle = yetty_wgpu.get_queue_handle()

    _adapter = YettyGPUAdapter(device_handle, queue_handle)
    _device = _adapter.request_device_sync()

    # Inject into pygfx's Shared
    from pygfx.renderers.wgpu.engine.shared import Shared
    if Shared._instance is None:
        Shared._selected_adapter = _adapter

    # Patch fastplotlib axes for None bbox
    _patch_axes()

    atexit.register(_cleanup_all)
    _initialized = True


def _patch_axes():
    """Patch fastplotlib axes to handle None bbox."""
    try:
        from fastplotlib.graphics._axes import Axes
        if hasattr(Axes, '_yetty_patched'):
            return

        orig = Axes.update_using_camera
        def patched(self):
            if self._plot_area.camera.fov != 0:
                bbox = self._plot_area._fpl_graphics_scene.get_world_bounding_box()
                if bbox is None:
                    return
            return orig(self)

        Axes.update_using_camera = patched
        Axes._yetty_patched = True
    except:
        pass


#-----------------------------------------------------------------------------
# Per-widget texture wrappers
#-----------------------------------------------------------------------------

_textures = {}  # widget_id -> pygfx Texture
_current_widget_id = None  # Set by init_widget, used as default


def set_current_widget(widget_id):
    """Set the current widget context (called from init_widget)."""
    global _current_widget_id
    _current_widget_id = widget_id


def get_current_widget():
    """Get the current widget ID."""
    return _current_widget_id


def init(widget_id=None):
    """
    Initialize pygfx for a widget. Call before creating figures.

    Args:
        widget_id: Widget ID from yetty_wgpu.allocate_widget_id()
                   If None, uses current widget set by set_current_widget()
    """
    global _textures

    if widget_id is None:
        widget_id = _current_widget_id
    if widget_id is None:
        raise RuntimeError("No widget_id provided and no current widget set")

    _init_shared()

    if widget_id in _textures:
        return _textures[widget_id]

    # Get texture from C++
    texture_handle = yetty_wgpu.get_render_texture_handle(widget_id)
    tex_w, tex_h = yetty_wgpu.get_render_texture_size(widget_id)

    # Create wgpu texture wrapper
    texture_ptr = ffi.cast('WGPUTexture', texture_handle)
    tex_info = {
        'size': (tex_w, tex_h, 1),
        'format': 'rgba8unorm',
        'mip_level_count': 1,
        'sample_count': 1,
        'dimension': '2d',
        'usage': (wgpu.TextureUsage.RENDER_ATTACHMENT |
                  wgpu.TextureUsage.TEXTURE_BINDING |
                  wgpu.TextureUsage.COPY_SRC |
                  wgpu.TextureUsage.COPY_DST),
    }
    wgpu_tex = GPUTexture("yetty-texture", texture_ptr, _device, tex_info)
    wgpu_tex._release_function = None

    # Create pygfx Texture wrapper
    import pygfx
    pygfx_tex = pygfx.Texture(
        data=None, dim=2,
        size=(tex_w, tex_h, 1),
        format='rgba8unorm',
    )
    pygfx_tex._wgpu_usage = tex_info['usage']
    pygfx_tex._wgpu_object = wgpu_tex

    # Add canvas-like methods for fastplotlib
    pygfx_tex._event_handlers = {}
    pygfx_tex._width = tex_w
    pygfx_tex._height = tex_h
    pygfx_tex._render_callback = None

    def add_event_handler(handler, *types):
        for t in types:
            pygfx_tex._event_handlers.setdefault(t, []).append(handler)

    def remove_event_handler(handler, *types):
        for t in types:
            if t in pygfx_tex._event_handlers:
                pygfx_tex._event_handlers[t] = [h for h in pygfx_tex._event_handlers[t] if h != handler]

    def request_draw(cb=None):
        if cb:
            pygfx_tex._render_callback = cb

    pygfx_tex.add_event_handler = add_event_handler
    pygfx_tex.remove_event_handler = remove_event_handler
    pygfx_tex.get_logical_size = lambda: (pygfx_tex._width, pygfx_tex._height)
    pygfx_tex.get_physical_size = lambda: (pygfx_tex._width, pygfx_tex._height)
    pygfx_tex.get_pixel_ratio = lambda: 1.0
    pygfx_tex.request_draw = request_draw
    pygfx_tex.close = lambda: None
    pygfx_tex.is_closed = lambda: False

    _textures[widget_id] = pygfx_tex
    return pygfx_tex


def get_texture(widget_id):
    """Get the pygfx texture for a widget."""
    return _textures.get(widget_id)


def create_figure(widget_id=None, **kwargs):
    """
    Create a fastplotlib Figure for a widget.

    Args:
        widget_id: Widget ID. If None, uses current widget.
        **kwargs: Passed to fpl.Figure (cameras, shape, etc.)

    Returns:
        fastplotlib Figure
    """
    if widget_id is None:
        widget_id = _current_widget_id
    if widget_id is None:
        raise RuntimeError("No widget_id provided and no current widget set")

    tex = _textures.get(widget_id)
    if not tex:
        tex = init(widget_id)

    import fastplotlib as fpl
    import pygfx

    w, h = tex._width, tex._height
    renderer = pygfx.WgpuRenderer(tex)

    return fpl.Figure(
        size=(w, h),
        canvas=tex,
        renderer=renderer,
        **kwargs
    )


_debug_frame_count = 0

def render_frame(widget_id=None):
    """Render one frame. Called by yetty each frame."""
    global _debug_frame_count
    _debug_frame_count += 1

    if widget_id is None:
        widget_id = _current_widget_id
    if widget_id is None:
        return False

    # Debug every 60 frames
    if _debug_frame_count % 60 == 0:
        callbacks_info = {wid: (tex._render_callback is not None) for wid, tex in _textures.items()}
        print(f"[render_frame] frame={_debug_frame_count} widget_id={widget_id} callbacks={callbacks_info}")

    tex = _textures.get(widget_id)
    if tex and tex._render_callback:
        try:
            tex._render_callback()
            return True
        except Exception as e:
            sys.stderr.write(f"render_frame error for widget {widget_id}: {e}\n")
    return False


def render_all_frames():
    """Render all widgets that have callbacks. Call this instead of render_frame for multi-widget support."""
    rendered = False
    for wid, tex in _textures.items():
        if tex and tex._render_callback:
            try:
                tex._render_callback()
                rendered = True
            except Exception as e:
                sys.stderr.write(f"render_all_frames error for widget {wid}: {e}\n")
    return rendered


def cleanup(widget_id):
    """Cleanup a widget's texture."""
    global _textures

    tex = _textures.pop(widget_id, None)
    if tex:
        try:
            tex._wgpu_object = None
        except:
            pass

    yetty_wgpu.cleanup_widget(widget_id)


def _cleanup_all():
    """Cleanup everything at exit."""
    global _textures, _adapter, _device, _initialized

    for wid in list(_textures.keys()):
        cleanup(wid)

    if _device:
        try:
            if hasattr(_device, '_poller') and _device._poller:
                _device._poller.stop()
        except:
            pass

    _device = None
    _adapter = None
    _initialized = False

    try:
        from pygfx.renderers.wgpu.engine.shared import Shared
        Shared._instance = None
        Shared._selected_adapter = None
    except:
        pass


# Convenience accessors
def get_device():
    return _device

def get_adapter():
    return _adapter
