"""ShaderToy plugin - WGSL shader rendering."""

import sys
import click
from pathlib import Path


@click.command()
@click.option('--input', '-i', 'input_', required=True, help='WGSL shader file (use - for stdin)')
@click.pass_context
def shadertoy(ctx, input_):
    """ShaderToy WGSL shader plugin.

    Renders WGSL fragment shaders in the terminal. Provides iTime, iResolution,
    and iMouse uniforms like ShaderToy.

    Example:
        yetty-client run shadertoy -i plasma.wgsl
        cat shader.wgsl | yetty-client run shadertoy -i -
    """
    ctx.ensure_object(dict)

    if input_ == '-':
        payload = sys.stdin.read()
    else:
        payload = Path(input_).read_text()

    ctx.obj['payload'] = payload
    ctx.obj['plugin_name'] = 'shader'
