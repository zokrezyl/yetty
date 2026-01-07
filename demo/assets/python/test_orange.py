"""Test - draw orange quad same way as green quad works"""
import sys
import yetty
import wgpu

@yetty.layer  
class OrangeTest:
    def init(self, ctx):
        from wgpu.backends.wgpu_native import ffi
        from wgpu.backends.wgpu_native._api import GPUDevice
        
        class NoRelease(GPUDevice):
            _release_function = None
        
        self.device = NoRelease.__new__(NoRelease)
        self.device._internal = ffi.cast('WGPUDevice', ctx.device_handle)
        self.device._adapter = None
        self.device._features = frozenset()
        self.device._limits = {}
        self.device._queue = None
        
        shader_code = """
@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> @builtin(position) vec4f {
    var pos = array<vec2f, 6>(
        vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
        vec2f(-1.0, 1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0)
    );
    return vec4f(pos[idx], 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
    return vec4f(1.0, 0.5, 0.0, 1.0);  // ORANGE
}
"""
        self.shader = self.device.create_shader_module(code=shader_code)
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
        sys.stderr.write("[OrangeTest] Pipeline created\n")
        sys.stderr.flush()

    def render(self, render_pass, frame, width, height):
        from wgpu.backends.wgpu_native import ffi
        from wgpu.backends.wgpu_native._api import GPURenderPassEncoder
        
        class NoReleasePass(GPURenderPassEncoder):
            _release_function = None
            def __del__(self): pass
        
        wgpu_pass = NoReleasePass.__new__(NoReleasePass)
        wgpu_pass._internal = ffi.cast('WGPURenderPassEncoder', render_pass.handle)
        wgpu_pass._device = None
        wgpu_pass._label = ""
        
        wgpu_pass.set_viewport(0, 0, float(width), float(height), 0.0, 1.0)
        wgpu_pass.set_pipeline(self.pipeline)
        wgpu_pass.draw(6, 1, 0, 0)
