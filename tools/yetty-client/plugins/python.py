"""Python plugin - execute Python code with pygfx rendering."""

import sys
import click
from pathlib import Path


@click.command()
@click.option('--input', '-i', 'input_', help='Python script file (use - for stdin)')
@click.option('--inline', '-I', 'inline_', help='Inline Python code')
@click.pass_context
def python(ctx, input_, inline_):
    """Python plugin for executing Python code with pygfx rendering.

    Execute Python scripts or inline code with access to pygfx graphics library.
    The plugin provides GPU-accelerated rendering through yetty's WebGPU context.

    Examples:
        yetty-client run python -i script.py
        yetty-client run python -I "print('Hello from Python')"
        cat script.py | yetty-client run python -i -

    The script should define a render(ctx, frame_num, width, height) callback
    that will be called every frame for animation.
    """
    ctx.ensure_object(dict)

    if input_ is None and inline_ is None:
        raise click.UsageError("Either --input or --inline must be specified")

    if input_ and inline_:
        raise click.UsageError("Cannot use both --input and --inline")

    if inline_:
        # Inline content - send as payload with inline: prefix
        payload = f"inline:{inline_}"
    elif input_ == '-':
        # Read from stdin
        content = sys.stdin.read()
        payload = f"inline:{content}"
    else:
        # File path - plugin will load it directly
        path = Path(input_)
        if not path.exists():
            raise click.ClickException(f"File not found: {input_}")
        payload = str(path.absolute())

    ctx.obj['payload'] = payload
    ctx.obj['plugin_name'] = 'python'
