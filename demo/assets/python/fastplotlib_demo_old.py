"""
Simple pygfx demo for yetty - direct rendering to yetty's render pass.
"""
import sys
import numpy as np
import yetty
import pygfx

sys.stderr.write(f"[Demo] pygfx version: {pygfx.__version__}\n")
sys.stderr.flush()


@yetty.layer
class SimpleDemo:
    """Simplest possible pygfx render test."""
    
    def init(self, ctx):
        sys.stderr.write(f"[Demo] init: {ctx.width}x{ctx.height}\n")
        sys.stderr.flush()
        
        from yetty_pygfx_direct import PygfxRenderer
        import yetty_pygfx_direct
        sys.stderr.write(f"[Demo] yetty_pygfx_direct loaded from: {yetty_pygfx_direct.__file__}\n")
        sys.stderr.flush()
        self.renderer = PygfxRenderer(ctx)
        
        # Create scene with a simple box - make it LARGE and BRIGHT
        self.scene = pygfx.Scene()
        
        # Camera that shows [-2, 2] range, centered at origin
        self.camera = pygfx.OrthographicCamera(4, 4)
        self.camera.local.position = (0, 0, 5)
        
        # BIG box, BRIGHT orange color, no depth test
        geometry = pygfx.box_geometry(3, 3, 0.1)  # Almost fills the view
        material = pygfx.MeshBasicMaterial(color=(1.0, 0.5, 0.0), depth_test=False)  # Bright orange
        self.box = pygfx.Mesh(geometry, material)
        self.scene.add(self.box)
        
        sys.stderr.write(f"[Demo] Scene created with box\n")
        sys.stderr.flush()
        
        # Create scene with a simple box - make it LARGE and BRIGHT
        self.scene = pygfx.Scene()
        
        # Camera that shows [-2, 2] range, centered at origin
        self.camera = pygfx.OrthographicCamera(4, 4)
        self.camera.local.position = (0, 0, 5)
        
        # BIG box, BRIGHT orange color, no depth test
        geometry = pygfx.box_geometry(3, 3, 0.1)  # Almost fills the view
        material = pygfx.MeshBasicMaterial(color=(1.0, 0.5, 0.0), depth_test=False)  # Bright orange
        self.box = pygfx.Mesh(geometry, material)
        self.scene.add(self.box)
        
        print("[Demo] Scene with LARGE orange box created")
        
    def render(self, render_pass, frame, width, height):
        self.renderer.render(render_pass, self.scene, self.camera)


print("[Demo] SimpleDemo registered")
