"""ShaderToy plugin - WGSL shader rendering."""

import sys
import click
from pathlib import Path


@click.command()
@click.option('--input', '-i', 'input_', required=True, help='WGSL shader file (use - for stdin)')
@click.option('--channel0', '-c0', help='Texture file for iChannel0')
@click.option('--channel1', '-c1', help='Texture file for iChannel1')
@click.option('--channel2', '-c2', help='Texture file for iChannel2')
@click.option('--channel3', '-c3', help='Texture file for iChannel3')
@click.pass_context
def shadertoy(ctx, input_, channel0, channel1, channel2, channel3):
    """ShaderToy WGSL shader plugin.

    Renders WGSL fragment shaders in the terminal. Provides iTime, iResolution,
    iMouse, and iChannel0-3 texture uniforms like ShaderToy.

    Example:
        yetty-client create shadertoy -i plasma.wgsl
        yetty-client create shadertoy -i warp.wgsl --channel0=texture.png
        cat shader.wgsl | yetty-client create shadertoy -i -
    """
    ctx.ensure_object(dict)

    if input_ == '-':
        payload = sys.stdin.read()
    else:
        payload = Path(input_).read_text()

    # Build plugin args for channel textures
    plugin_args = []
    if channel0:
        plugin_args.append(f"--channel0={Path(channel0).resolve()}")
    if channel1:
        plugin_args.append(f"--channel1={Path(channel1).resolve()}")
    if channel2:
        plugin_args.append(f"--channel2={Path(channel2).resolve()}")
    if channel3:
        plugin_args.append(f"--channel3={Path(channel3).resolve()}")

    ctx.obj['payload'] = payload
    ctx.obj['plugin_name'] = 'shader'
    ctx.obj['plugin_args'] = ' '.join(plugin_args)
