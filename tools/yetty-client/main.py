#!/usr/bin/env python3
"""Yetty Client - CLI tool for yetty terminal plugins.

Usage:
    python main.py [global-options] <command> [plugin-name] [plugin-options]

Examples:
    python main.py plugins                          # List available plugins
    python main.py run ymery --layout=/path        # Activate ymery plugin
    python main.py dump shadertoy -s=shader.wgsl   # Save OSC to file
    python main.py ls                              # List active plugins
"""

import sys
from pathlib import Path

# Add package to path
sys.path.insert(0, str(Path(__file__).parent))

import click
from core import osc, base94
from plugins import discover_plugins, get_plugin, list_plugins


# Common position options for run/dump commands
def position_options(f):
    """Decorator to add common position options."""
    f = click.option('--mode', '-M', type=click.Choice(['A', 'R']), default='R',
                     help='Position mode: A=absolute, R=relative to cursor')(f)
    f = click.option('--x', '-x', 'pos_x', default=0, type=int, help='X position in cells')(f)
    f = click.option('--y', '-y', 'pos_y', default=0, type=int, help='Y position in cells')(f)
    f = click.option('--width', '-w', 'width', default=40, type=int, help='Width in cells')(f)
    f = click.option('--height', '-H', 'height', default=20, type=int, help='Height in cells')(f)
    return f


@click.group()
@click.option('--verbose', '-v', is_flag=True, help='Enable verbose output')
@click.option('--dry-run', '-n', is_flag=True, help='Show what would be done')
@click.pass_context
def cli(ctx, verbose, dry_run):
    """Yetty Client - manage yetty terminal plugins."""
    ctx.ensure_object(dict)
    ctx.obj['verbose'] = verbose
    ctx.obj['dry_run'] = dry_run


@cli.command('plugins')
@click.pass_context
def cmd_plugins(ctx):
    """List available client plugins."""
    plugins = discover_plugins()
    if not plugins:
        click.echo("No plugins found.")
        return

    click.echo("Available plugins:")
    for name, cmd in sorted(plugins.items()):
        doc = cmd.help or cmd.callback.__doc__ or ''
        first_line = doc.split('\n')[0].strip() if doc else ''
        click.echo(f"  {name:15} {first_line}")


@cli.command('ls')
@click.pass_context
def cmd_ls(ctx):
    """List active plugins in the terminal.

    Sends a query OSC sequence to yetty and displays the response.
    Note: Requires yetty query support (OSC 99999;?;Q).
    """
    if ctx.obj['dry_run']:
        click.echo("Would send query: " + repr(osc.create_query_sequence()))
        return

    # Send query sequence (wrap for tmux if needed)
    query = osc.create_query_sequence()
    sys.stdout.write(osc.maybe_wrap_for_tmux(query))
    sys.stdout.flush()

    # TODO: Read and parse response from terminal
    click.echo("Query sent. (Response parsing not yet implemented)", err=True)


@cli.command('run', context_settings=dict(
    ignore_unknown_options=True,
    allow_extra_args=True,
), add_help_option=False)
@click.argument('plugin_name')
@position_options
@click.pass_context
def cmd_run(ctx, plugin_name, mode, pos_x, pos_y, width, height):
    """Activate a plugin in the terminal.

    Outputs the OSC escape sequence directly to stdout, which activates
    the plugin when running inside yetty.

    Example:
        yetty-client run ymery -p /path/to/layouts -m app -w 60 -H 30
    """
    # Get the plugin command
    try:
        plugin_cmd = get_plugin(plugin_name)
    except KeyError as e:
        raise click.ClickException(str(e))

    # Show combined help if --help is in args
    if '--help' in ctx.args or '-h' in ctx.args:
        click.echo(f"Usage: yetty-client run [POSITION OPTIONS] {plugin_name} [PLUGIN OPTIONS]\n")
        click.echo("Position options:")
        click.echo("  -M, --mode [A|R]      Position mode: A=absolute, R=relative (default: R)")
        click.echo("  -x, --x INTEGER       X position in cells (default: 0)")
        click.echo("  -y, --y INTEGER       Y position in cells (default: 0)")
        click.echo("  -w, --width INTEGER   Width in cells (default: 40)")
        click.echo("  -H, --height INTEGER  Height in cells (default: 20)")
        click.echo("")
        with click.Context(plugin_cmd, info_name=plugin_name) as plugin_ctx:
            click.echo(plugin_cmd.get_help(plugin_ctx))
        return

    # Parse remaining args with the plugin command
    plugin_ctx = plugin_cmd.make_context(plugin_name, ctx.args, parent=ctx)

    with plugin_ctx:
        # Invoke the plugin
        plugin_cmd.invoke(plugin_ctx)

        payload = plugin_ctx.obj.get('payload') if plugin_ctx.obj else None
        payload_bytes = plugin_ctx.obj.get('payload_bytes') if plugin_ctx.obj else None
        actual_plugin_name = plugin_ctx.obj.get('plugin_name', plugin_name) if plugin_ctx.obj else plugin_name

    if payload is None and payload_bytes is None:
        raise click.ClickException(f"Plugin '{plugin_name}' did not set a payload")

    # Generate OSC sequence
    if payload_bytes is not None:
        encoded = base94.encode(payload_bytes)
        sequence = f"\033]{osc.VENDOR_ID};{actual_plugin_name};{mode};{pos_x};{pos_y};{width};{height};{encoded}\033\\"
    else:
        sequence = osc.create_sequence(actual_plugin_name, mode, pos_x, pos_y, width, height, payload)

    if ctx.obj['dry_run']:
        if osc.is_inside_tmux():
            wrapped = osc.wrap_for_tmux(sequence)
            click.echo(f"Would output OSC sequence ({len(sequence)} bytes, {len(wrapped)} with tmux wrapper)")
        else:
            click.echo(f"Would output OSC sequence ({len(sequence)} bytes)")
        if ctx.obj['verbose']:
            click.echo(f"Plugin: {actual_plugin_name}")
            click.echo(f"Mode: {mode}, Position: ({pos_x},{pos_y}), Size: {width}x{height}")
            if payload:
                click.echo(f"Payload: {payload[:100]}{'...' if len(payload) > 100 else ''}")
        return

    # Output to stdout (wrap for tmux if needed)
    output_sequence = osc.maybe_wrap_for_tmux(sequence)
    sys.stdout.write(output_sequence)
    sys.stdout.flush()

    if ctx.obj['verbose']:
        if osc.is_inside_tmux():
            click.echo(f"Sent {len(output_sequence)} bytes (tmux passthrough)", err=True)
        else:
            click.echo(f"Sent {len(sequence)} bytes", err=True)


@cli.command('dump', context_settings=dict(
    ignore_unknown_options=True,
    allow_extra_args=True,
), add_help_option=False)
@click.argument('plugin_name')
@click.option('--output', '-o', type=click.Path(), help='Output file (default: <plugin>.osc)')
@position_options
@click.pass_context
def cmd_dump(ctx, plugin_name, output, mode, pos_x, pos_y, width, height):
    """Save plugin OSC sequence to a file.

    The output file can be later cat'd to the terminal to activate the plugin.

    Example:
        yetty-client dump shadertoy -i shader.wgsl -o demo.osc
        cat demo.osc  # activates the shader in yetty
    """
    # Get the plugin command
    try:
        plugin_cmd = get_plugin(plugin_name)
    except KeyError as e:
        raise click.ClickException(str(e))

    # Show combined help if --help is in args
    if '--help' in ctx.args or '-h' in ctx.args:
        click.echo(f"Usage: yetty-client dump [OPTIONS] {plugin_name} [PLUGIN OPTIONS]\n")
        click.echo("Options:")
        click.echo("  -o, --output PATH     Output file (default: <plugin>.osc)")
        click.echo("  -M, --mode [A|R]      Position mode: A=absolute, R=relative (default: R)")
        click.echo("  -x, --x INTEGER       X position in cells (default: 0)")
        click.echo("  -y, --y INTEGER       Y position in cells (default: 0)")
        click.echo("  -w, --width INTEGER   Width in cells (default: 40)")
        click.echo("  -H, --height INTEGER  Height in cells (default: 20)")
        click.echo("")
        with click.Context(plugin_cmd, info_name=plugin_name) as plugin_ctx:
            click.echo(plugin_cmd.get_help(plugin_ctx))
        return

    # Parse remaining args with the plugin command
    plugin_ctx = plugin_cmd.make_context(plugin_name, ctx.args, parent=ctx)

    with plugin_ctx:
        plugin_cmd.invoke(plugin_ctx)

        payload = plugin_ctx.obj.get('payload') if plugin_ctx.obj else None
        payload_bytes = plugin_ctx.obj.get('payload_bytes') if plugin_ctx.obj else None
        actual_plugin_name = plugin_ctx.obj.get('plugin_name', plugin_name) if plugin_ctx.obj else plugin_name

    if payload is None and payload_bytes is None:
        raise click.ClickException(f"Plugin '{plugin_name}' did not set a payload")

    # Generate OSC sequence
    if payload_bytes is not None:
        encoded = base94.encode(payload_bytes)
        sequence = f"\033]{osc.VENDOR_ID};{actual_plugin_name};{mode};{pos_x};{pos_y};{width};{height};{encoded}\033\\"
    else:
        sequence = osc.create_sequence(actual_plugin_name, mode, pos_x, pos_y, width, height, payload)

    # Determine output file
    if output is None:
        output = f"{plugin_name}.osc"

    if ctx.obj['dry_run']:
        click.echo(f"Would write {len(sequence)} bytes to {output}")
        return

    with open(output, 'w') as f:
        f.write(sequence)

    click.echo(f"Saved to: {output} ({len(sequence)} bytes)")


@cli.command('decode')
@click.argument('input_file', type=click.Path(exists=True))
@click.option('--output', '-o', type=click.Path(), help='Output file (default: stdout)')
@click.option('--info', '-i', is_flag=True, help='Show sequence metadata only')
@click.option('--raw', '-r', is_flag=True, help='Output raw bytes (for binary payloads like images)')
@click.pass_context
def cmd_decode(ctx, input_file, output, info, raw):
    """Decode a yetty OSC sequence file.

    Parses .yetty or .osc files and extracts the decoded payload.
    Useful for inspecting or recovering the original content.

    Example:
        yetty-client decode demo/files/shader.encoded.yetty
        yetty-client decode demo.osc -o shader.wgsl
        yetty-client decode demo.osc --info
    """
    with open(input_file, 'r') as f:
        content = f.read()

    # Strip trailing whitespace/newlines
    content = content.rstrip('\r\n')

    # Strip OSC terminators: BEL (0x07) or ST (ESC \)
    if content.endswith('\x07'):
        content = content[:-1]
    elif content.endswith('\x1b\\'):
        content = content[:-2]

    # Strip leading ESC if present (ESC = 0x1B)
    if content.startswith('\x1b]'):
        content = content[2:]
    elif content.startswith(']'):
        content = content[1:]
    else:
        raise click.ClickException(f"Invalid yetty file: expected OSC sequence starting with ESC ]")

    parts = content.split(';', 7)
    if len(parts) < 8:
        raise click.ClickException(f"Invalid OSC sequence: expected 8 parts, got {len(parts)}")

    vendor_id, plugin, mode, x, y, w, h, encoded_payload = parts

    if vendor_id != str(osc.VENDOR_ID):
        raise click.ClickException(f"Unknown vendor ID: {vendor_id} (expected {osc.VENDOR_ID})")

    # Show metadata
    if info or ctx.obj['verbose']:
        click.echo(f"Plugin:   {plugin}", err=True)
        click.echo(f"Mode:     {mode} ({'absolute' if mode == 'A' else 'relative'})", err=True)
        click.echo(f"Position: ({x}, {y})", err=True)
        click.echo(f"Size:     {w}x{h}", err=True)
        click.echo(f"Payload:  {len(encoded_payload)} chars (base94)", err=True)

    if info:
        return

    # Decode payload
    try:
        if raw:
            decoded_bytes = base94.decode(encoded_payload)
        else:
            decoded_bytes = base94.decode(encoded_payload)
            try:
                decoded_text = decoded_bytes.decode('utf-8')
            except UnicodeDecodeError:
                raise click.ClickException(
                    f"Payload is not valid UTF-8 text. Use --raw for binary data.")
    except Exception as e:
        raise click.ClickException(f"Failed to decode payload: {e}")

    if ctx.obj['verbose']:
        click.echo(f"Decoded:  {len(decoded_bytes)} bytes", err=True)

    if ctx.obj['dry_run']:
        click.echo(f"Would write {len(decoded_bytes)} bytes to {'stdout' if not output else output}")
        return

    if raw:
        if output:
            with open(output, 'wb') as f:
                f.write(decoded_bytes)
            click.echo(f"Saved to: {output} ({len(decoded_bytes)} bytes)", err=True)
        else:
            sys.stdout.buffer.write(decoded_bytes)
    else:
        if output:
            with open(output, 'w') as f:
                f.write(decoded_text)
            click.echo(f"Saved to: {output} ({len(decoded_text)} bytes)", err=True)
        else:
            click.echo(decoded_text)


@cli.command('help')
@click.argument('topic', required=False)
@click.pass_context
def cmd_help(ctx, topic):
    """Show help for a command or plugin.

    Examples:
        python main.py help          # General help
        python main.py help run      # Help for 'run' command
        python main.py help ymery    # Help for 'ymery' plugin
    """
    if topic is None:
        click.echo(ctx.parent.get_help())
        return

    # Check if it's a command
    if topic in cli.commands:
        cmd = cli.commands[topic]
        with click.Context(cmd, info_name=topic, parent=ctx) as sub_ctx:
            click.echo(cmd.get_help(sub_ctx))
        return

    # Check if it's a plugin
    try:
        plugin_cmd = get_plugin(topic)
        click.echo(f"Plugin: {topic}\n")
        with click.Context(plugin_cmd, info_name=topic) as plugin_ctx:
            click.echo(plugin_cmd.get_help(plugin_ctx))
    except KeyError:
        raise click.ClickException(f"Unknown topic: '{topic}'. Try 'plugins' to list available plugins.")


def main():
    """Entry point."""
    cli(obj={})


if __name__ == '__main__':
    main()
