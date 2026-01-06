"""
yetty_pygfx - Direct integration of pygfx/fastplotlib with yetty's WebGPU context.

This module hooks into pygfx to use yetty's WebGPU device and render directly
to yetty's texture (no offscreen rendering or pixel copying).

Must be imported BEFORE any pygfx/wgpu imports to work correctly.
"""

import os
import sys
import atexit
import gc

# Step 1: Point wgpu-py to yetty's wgpu-native library
# This MUST be done before importing wgpu
_yetty_wgpu_lib = os.environ.get('YETTY_WGPU_LIB_PATH')
if _yetty_wgpu_lib:
    os.environ['WGPU_LIB_PATH'] = _yetty_wgpu_lib

# Force offscreen mode to avoid glfw issues
os.environ['RENDERCANVAS_FORCE_OFFSCREEN'] = '1'

# Now import yetty_wgpu (our C++ module with handles)
import yetty_wgpu

# Import wgpu - it will now use yetty's wgpu-native
import wgpu
from wgpu.backends.wgpu_native import ffi, lib, GPUDevice, GPUQueue, GPUAdapter, GPUAdapterInfo
from wgpu.backends.wgpu_native._api import (
    GPUTexture, GPUTextureView, get_wgpu_instance,
    _get_limits, _get_features
)


def _create_adapter_info():
    """Create a minimal adapter info object."""
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
        # Don't call super().__init__ - we're faking it
        self._device_handle = device_handle
        self._queue_handle = queue_handle
        self._internal = None
        self._features = frozenset(['float32-filterable'])  # pygfx needs this
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
        """Return yetty's device wrapped as a GPUDevice."""
        return create_yetty_device(self._device_handle, self._queue_handle, self)


def create_yetty_device(device_handle, queue_handle, adapter):
    """Create a GPUDevice wrapping yetty's device handle."""

    # Cast integer handles to cffi pointers
    device_ptr = ffi.cast('WGPUDevice', device_handle)
    queue_ptr = ffi.cast('WGPUQueue', queue_handle)

    # Get features from adapter
    features = set(adapter.features)

    # Get limits from the device
    try:
        limits = _get_limits(device_ptr, device=True)
    except Exception as e:
        print(f"Warning: Could not get device limits: {e}")
        limits = {}

    # Create queue first (device=None initially)
    queue = GPUQueue("yetty-queue", queue_ptr, None)

    # Create device
    device = GPUDevice("yetty-device", device_ptr, adapter, features, limits, queue)

    # Keep the polling thread running - it's needed for wgpu operations
    # The poller calls wgpuDevicePoll() which is fine for yetty's device

    # Mark that we shouldn't release these handles (yetty owns them)
    device._release_function = None
    queue._release_function = None

    return device


def create_wgpu_texture(texture_handle, width, height, device):
    """Create a wgpu GPUTexture wrapping yetty's texture handle."""
    texture_ptr = ffi.cast('WGPUTexture', texture_handle)
    tex_info = {
        'size': (width, height, 1),
        'format': 'rgba8unorm',
        'mip_level_count': 1,
        'sample_count': 1,
        'dimension': '2d',
        'usage': (wgpu.TextureUsage.RENDER_ATTACHMENT |
                  wgpu.TextureUsage.TEXTURE_BINDING |
                  wgpu.TextureUsage.COPY_SRC |
                  wgpu.TextureUsage.COPY_DST),
    }
    wgpu_texture = GPUTexture("yetty-texture", texture_ptr, device, tex_info)
    wgpu_texture._release_function = None  # Don't release - yetty owns this
    return wgpu_texture


# Global state
_yetty_device = None
_yetty_adapter = None
_yetty_wgpu_texture = None
_pygfx_texture = None
_figure = None
_render_callback = None
_initialized = False


def init():
    """
    Initialize yetty-pygfx integration.
    Must be called before creating any pygfx renderers/figures.
    """
    global _yetty_device, _yetty_adapter, _yetty_wgpu_texture, _pygfx_texture, _initialized

    if _initialized:
        return True

    if not yetty_wgpu.is_initialized():
        raise RuntimeError("yetty_wgpu not initialized. Run from yetty first.")

    # Get handles from yetty
    device_handle = yetty_wgpu.get_device_handle()
    queue_handle = yetty_wgpu.get_queue_handle()
    texture_handle = yetty_wgpu.get_render_texture_handle()
    tex_width, tex_height = yetty_wgpu.get_render_texture_size()

    # Create adapter and device wrappers
    _yetty_adapter = YettyGPUAdapter(device_handle, queue_handle)
    _yetty_device = _yetty_adapter.request_device_sync()

    # Create wgpu texture wrapper
    _yetty_wgpu_texture = create_wgpu_texture(texture_handle, tex_width, tex_height, _yetty_device)

    # Inject into pygfx's Shared system BEFORE any renderer is created
    from pygfx.renderers.wgpu.engine.shared import Shared

    if Shared._instance is None:
        Shared._selected_adapter = _yetty_adapter
        print("yetty_pygfx: Injected yetty adapter into pygfx")
    else:
        raise RuntimeError("pygfx Shared already created - init() must be called earlier")

    # Create a pygfx Texture wrapping the wgpu texture
    import pygfx
    _pygfx_texture = pygfx.Texture(
        data=None,
        dim=2,
        size=(tex_width, tex_height, 1),
        format='rgba8unorm',
    )

    # Set usage flags
    _pygfx_texture._wgpu_usage = (
        wgpu.TextureUsage.RENDER_ATTACHMENT |
        wgpu.TextureUsage.TEXTURE_BINDING |
        wgpu.TextureUsage.COPY_SRC |
        wgpu.TextureUsage.COPY_DST
    )

    # Inject the wgpu texture
    _pygfx_texture._wgpu_object = _yetty_wgpu_texture

    # Add canvas-like methods needed by fastplotlib
    _pygfx_texture._event_handlers = {}
    _pygfx_texture._width = tex_width
    _pygfx_texture._height = tex_height

    def add_event_handler(handler, *event_types):
        for event_type in event_types:
            if event_type not in _pygfx_texture._event_handlers:
                _pygfx_texture._event_handlers[event_type] = []
            _pygfx_texture._event_handlers[event_type].append(handler)

    def remove_event_handler(handler, *event_types):
        for event_type in event_types:
            if event_type in _pygfx_texture._event_handlers:
                _pygfx_texture._event_handlers[event_type] = [
                    h for h in _pygfx_texture._event_handlers[event_type] if h != handler
                ]

    def get_logical_size():
        return (_pygfx_texture._width, _pygfx_texture._height)

    def get_physical_size():
        return (_pygfx_texture._width, _pygfx_texture._height)

    def get_pixel_ratio():
        return 1.0

    def request_draw(callback=None):
        global _render_callback
        if callback is not None:
            _render_callback = callback
        # Don't call callback here - render_frame() will do it

    def close():
        pass

    def is_closed():
        return False

    _pygfx_texture.add_event_handler = add_event_handler
    _pygfx_texture.remove_event_handler = remove_event_handler
    _pygfx_texture.get_logical_size = get_logical_size
    _pygfx_texture.get_physical_size = get_physical_size
    _pygfx_texture.get_pixel_ratio = get_pixel_ratio
    _pygfx_texture.request_draw = request_draw
    _pygfx_texture.close = close
    _pygfx_texture.is_closed = is_closed

    # Register atexit handler to ensure cleanup happens before Python finalizes
    atexit.register(_atexit_cleanup)

    _initialized = True
    return True


def _atexit_cleanup():
    """Called at exit to clean up before Python finalizes."""
    # Just call cleanup() which handles everything
    cleanup()


def get_device():
    """Get the yetty-wrapped GPUDevice."""
    return _yetty_device


def get_adapter():
    """Get the yetty-wrapped GPUAdapter."""
    return _yetty_adapter


def get_texture():
    """Get the pygfx texture (with yetty's wgpu texture inside)."""
    return _pygfx_texture


def create_figure(width=None, height=None):
    """Create a fastplotlib Figure using yetty's WebGPU context."""
    global _figure

    if not _initialized:
        init()

    import fastplotlib as fpl
    import pygfx

    # Use yetty's texture size if not specified
    if width is None or height is None:
        tex_width, tex_height = yetty_wgpu.get_render_texture_size()
        width = width or tex_width
        height = height or tex_height

    # Create renderer with our pygfx texture as target
    renderer = pygfx.WgpuRenderer(_pygfx_texture)

    # Create figure with our texture
    _figure = fpl.Figure(
        size=(width, height),
        canvas=_pygfx_texture,
        renderer=renderer,
    )

    return _figure


def render_frame():
    """Render one frame. Called by yetty each frame."""
    global _render_callback

    if _render_callback is not None:
        try:
            _render_callback()
            return True
        except Exception as e:
            import sys
            sys.stderr.write(f"render_frame error: {e}\n")
            import traceback
            traceback.print_exc(file=sys.stderr)
            return False
    return False


def cleanup():
    """Cleanup resources."""
    global _yetty_device, _yetty_adapter, _yetty_wgpu_texture, _pygfx_texture
    global _figure, _render_callback, _initialized

    if not _initialized:
        return

    _initialized = False  # Mark as not initialized to prevent re-entry

    # Clear the render callback first to stop any rendering
    _render_callback = None

    # Stop ALL device pollers to prevent accessing cleaned-up resources
    if _yetty_device is not None:
        try:
            if hasattr(_yetty_device, '_poller') and _yetty_device._poller is not None:
                _yetty_device._poller.stop()
                _yetty_device._poller = None
        except:
            pass

    # Also stop the shared device's poller if different
    try:
        from pygfx.renderers.wgpu.engine.shared import Shared
        if Shared._instance is not None and hasattr(Shared._instance, '_device'):
            dev = Shared._instance._device
            if dev is not None and hasattr(dev, '_poller') and dev._poller is not None:
                dev._poller.stop()
                dev._poller = None
    except:
        pass

    # Force garbage collection to clean up Python wgpu objects
    gc.collect()

    # Clear figure reference
    if _figure is not None:
        try:
            # Try to close the renderer
            if hasattr(_figure, 'renderer'):
                _figure.renderer = None
        except:
            pass
        _figure = None

    # Clear pygfx texture reference (but don't release - yetty owns it)
    if _pygfx_texture is not None:
        try:
            # Clear the wgpu object reference
            _pygfx_texture._wgpu_object = None
            _pygfx_texture._wgpu_default_view = None
        except:
            pass
        _pygfx_texture = None

    # Clear wgpu texture (don't release - yetty owns it)
    _yetty_wgpu_texture = None

    # Clear device and adapter (don't release - yetty owns them)
    _yetty_device = None
    _yetty_adapter = None

    # Reset pygfx's Shared instance to avoid issues on next init
    try:
        from pygfx.renderers.wgpu.engine.shared import Shared
        Shared._instance = None
        Shared._selected_adapter = None
    except:
        pass

    _initialized = False


__all__ = [
    'init',
    'get_device',
    'get_adapter',
    'get_texture',
    'create_figure',
    'render_frame',
    'cleanup',
]
