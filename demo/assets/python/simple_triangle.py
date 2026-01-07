"""
Simple triangle test - bypass pygfx entirely.
"""
import yetty

print("[Triangle] Simple triangle demo")

@yetty.layer
class TriangleDemo:
    def init(self, ctx):
        print(f"[Triangle] init: {ctx.width}x{ctx.height}")
        self.width = ctx.width
        self.height = ctx.height
        
        # We'll just try to draw something to confirm render pass works
        import wgpu
        from wgpu.backends.wgpu_native import ffi
        
        device_ptr = ffi.cast('WGPUDevice', ctx.device_handle)
        
        # Create a simple device wrapper
        from wgpu.backends.wgpu_native._api import GPUDevice, GPUQueue
        
        class NoReleaseDevice(GPUDevice):
            _release_function = None
        
        self.device = NoReleaseDevice.__new__(NoReleaseDevice)
        self.device._internal = device_ptr
        self.device._adapter = None
        self.device._features = frozenset()
        self.device._limits = {}
        self.device._queue = None
        
        # Create a simple shader
        shader_code = """
@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> @builtin(position) vec4f {
    var pos = array<vec2f, 3>(
        vec2f(0.0, 0.5),
        vec2f(-0.5, -0.5),
        vec2f(0.5, -0.5)
    );
    return vec4f(pos[idx], 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
    return vec4f(1.0, 0.5, 0.0, 1.0);  // Orange
}
"""
        
        self.shader = self.device.create_shader_module(code=shader_code)
        print(f"[Triangle] Shader created: {self.shader}")
        
        self.pipeline = self.device.create_render_pipeline(
            layout="auto",
            vertex={"module": self.shader, "entry_point": "vs_main"},
            fragment={
                "module": self.shader,
                "entry_point": "fs_main",
                "targets": [{"format": wgpu.TextureFormat.bgra8unorm_srgb}],
            },
            primitive={"topology": wgpu.PrimitiveTopology.triangle_list},
        )
        print(f"[Triangle] Pipeline created: {self.pipeline}")
        
    def render(self, render_pass, frame, width, height):
        import sys
        from wgpu.backends.wgpu_native import ffi
        from wgpu.backends.wgpu_native._api import GPURenderPassEncoder
        
        # Wrap the render pass
        class NoReleasePass(GPURenderPassEncoder):
            _release_function = None
            def __del__(self): pass
        
        wgpu_pass = NoReleasePass.__new__(NoReleasePass)
        wgpu_pass._internal = ffi.cast('WGPURenderPassEncoder', render_pass.handle)
        wgpu_pass._device = None
        wgpu_pass._label = ""
        
        # Set viewport
        wgpu_pass.set_viewport(0, 0, float(width), float(height), 0.0, 1.0)
        
        # Draw triangle
        wgpu_pass.set_pipeline(self.pipeline)
        wgpu_pass.draw(3, 1, 0, 0)
        
        sys.stderr.write(f"[Triangle] Drew triangle frame {frame}\n")
        sys.stderr.flush()

print("[Triangle] Registered")
