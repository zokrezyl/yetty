"""Image plugin - display images in terminal."""

import sys
import click
from pathlib import Path


@click.command()
@click.option('--input', '-i', 'input_', required=True, help='Image file (use - for stdin)')
@click.pass_context
def image(ctx, input_):
    """Image display plugin.

    Displays images in the terminal using WebGPU rendering.
    Supports PNG, JPG, GIF, and BMP formats.

    Example:
        yetty-client run image -i logo.png
        cat logo.png | yetty-client run image -i -
    """
    ctx.ensure_object(dict)

    if input_ == '-':
        image_data = sys.stdin.buffer.read()
    else:
        image_data = Path(input_).read_bytes()

    ctx.obj['payload_bytes'] = image_data
    ctx.obj['plugin_name'] = 'image'
