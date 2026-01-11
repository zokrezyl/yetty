#!/usr/bin/env python3
"""Yetty Client - CLI tool for yetty terminal plugins.

Usage:
    yetty-client create <plugin> [options]     # Create a plugin layer
    yetty-client ls [--all]                    # List active layers
    yetty-client plugins                       # List available plugins
    yetty-client kill --id ID | --plugin NAME  # Kill layer(s)
    yetty-client stop --id ID | --plugin NAME  # Stop layer(s)
    yetty-client start --id ID | --plugin NAME # Start layer(s)

Examples:
    yetty-client create image -f logo.png -w 40 -h 20
    yetty-client create shader -f shader.wgsl
    yetty-client kill --plugin image
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import click
from core import osc, base94
from plugins import discover_plugins, get_plugin


@click.group()
@click.option('--verbose', '-v', is_flag=True, help='Enable verbose output')
@click.option('--dry-run', '-n', is_flag=True, help='Show what would be done')
@click.pass_context
def cli(ctx, verbose, dry_run):
    """Yetty Client - manage yetty terminal plugins."""
    ctx.ensure_object(dict)
    ctx.obj['verbose'] = verbose
    ctx.obj['dry_run'] = dry_run


@cli.command('create', context_settings=dict(
    ignore_unknown_options=True,
    allow_extra_args=True,
), add_help_option=False)
@click.argument('plugin_name')
@click.option('--x', '-x', 'pos_x', default=0, type=int, help='X position in cells')
@click.option('--y', '-y', 'pos_y', default=0, type=int, help='Y position in cells')
@click.option('--width', '-w', 'width', default=0, type=int, help='Width in cells (0=stretch)')
@click.option('--height', '-H', 'height', default=0, type=int, help='Height in cells (0=stretch)')
@click.option('--absolute', '-a', is_flag=True, help='Use absolute positioning')
@click.option('--output', '-o', type=click.Path(), help='Save to file instead of stdout')
@click.pass_context
def cmd_create(ctx, plugin_name, pos_x, pos_y, width, height, absolute, output):
    """Create a plugin layer.

    Example:
        yetty-client create image -f logo.png -w 40 -H 20
        yetty-client create shader -f shader.wgsl -w 60 -H 30
    """
    try:
        plugin_cmd = get_plugin(plugin_name)
    except KeyError as e:
        raise click.ClickException(str(e))

    if '--help' in ctx.args or '-h' in ctx.args:
        click.echo(f"Usage: yetty-client create {plugin_name} [OPTIONS]\n")
        click.echo("Position options:")
        click.echo("  -x INTEGER       X position in cells (default: 0)")
        click.echo("  -y INTEGER       Y position in cells (default: 0)")
        click.echo("  -w INTEGER       Width in cells (default: 0, stretch)")
        click.echo("  -H INTEGER       Height in cells (default: 0, stretch)")
        click.echo("  -a, --absolute   Use absolute positioning")
        click.echo("  -o, --output     Save to file instead of stdout")
        click.echo("")
        with click.Context(plugin_cmd, info_name=plugin_name) as plugin_ctx:
            click.echo(plugin_cmd.get_help(plugin_ctx))
        return

    plugin_ctx = plugin_cmd.make_context(plugin_name, ctx.args, parent=ctx)

    with plugin_ctx:
        plugin_cmd.invoke(plugin_ctx)
        payload = plugin_ctx.obj.get('payload') if plugin_ctx.obj else None
        payload_bytes = plugin_ctx.obj.get('payload_bytes') if plugin_ctx.obj else None
        actual_plugin = plugin_ctx.obj.get('plugin_name', plugin_name) if plugin_ctx.obj else plugin_name
        plugin_args = plugin_ctx.obj.get('plugin_args', '') if plugin_ctx.obj else ''

    if payload is None and payload_bytes is None:
        raise click.ClickException(f"Plugin '{plugin_name}' did not set a payload")

    if payload_bytes is not None:
        sequence = osc.create_sequence_bytes(
            plugin=actual_plugin, x=pos_x, y=pos_y, w=width, h=height,
            relative=not absolute, payload_bytes=payload_bytes,
            plugin_args=plugin_args
        )
    else:
        sequence = osc.create_sequence(
            plugin=actual_plugin, x=pos_x, y=pos_y, w=width, h=height,
            relative=not absolute, payload=payload,
            plugin_args=plugin_args
        )

    if ctx.obj['dry_run']:
        click.echo(f"Would output OSC sequence ({len(sequence)} bytes)")
        if ctx.obj['verbose']:
            click.echo(f"Plugin: {actual_plugin}, Pos: ({pos_x},{pos_y}), Size: {width}x{height}")
        return

    if output:
        with open(output, 'w') as f:
            f.write(sequence)
        click.echo(f"Saved to: {output} ({len(sequence)} bytes)")
    else:
        sys.stdout.write(osc.maybe_wrap_for_tmux(sequence))
        sys.stdout.flush()


@cli.command('ls')
@click.option('--all', '-a', 'show_all', is_flag=True, help='Include stopped layers')
@click.pass_context
def cmd_ls(ctx, show_all):
    """List active layers in the terminal."""
    sequence = osc.list_sequence(all=show_all)
    if ctx.obj['dry_run']:
        click.echo(f"Would send: {repr(sequence)}")
        return
    sys.stdout.write(osc.maybe_wrap_for_tmux(sequence))
    sys.stdout.flush()


@cli.command('plugins')
@click.pass_context
def cmd_plugins(ctx):
    """List available plugins (terminal-side)."""
    sequence = osc.plugins_sequence()
    if ctx.obj['dry_run']:
        click.echo(f"Would send: {repr(sequence)}")
        return
    sys.stdout.write(osc.maybe_wrap_for_tmux(sequence))
    sys.stdout.flush()


@cli.command('kill')
@click.option('--id', 'layer_id', help='Layer ID to kill')
@click.option('--plugin', '-p', 'plugin_name', help='Kill all layers of this plugin')
@click.pass_context
def cmd_kill(ctx, layer_id, plugin_name):
    """Kill (destroy) layer(s)."""
    if not layer_id and not plugin_name:
        raise click.ClickException("Either --id or --plugin is required")
    sequence = osc.kill_sequence(id=layer_id, plugin=plugin_name)
    if ctx.obj['dry_run']:
        click.echo(f"Would send: {repr(sequence)}")
        return
    sys.stdout.write(osc.maybe_wrap_for_tmux(sequence))
    sys.stdout.flush()


@cli.command('stop')
@click.option('--id', 'layer_id', help='Layer ID to stop')
@click.option('--plugin', '-p', 'plugin_name', help='Stop all layers of this plugin')
@click.pass_context
def cmd_stop(ctx, layer_id, plugin_name):
    """Stop (pause) layer(s)."""
    if not layer_id and not plugin_name:
        raise click.ClickException("Either --id or --plugin is required")
    sequence = osc.stop_sequence(id=layer_id, plugin=plugin_name)
    if ctx.obj['dry_run']:
        click.echo(f"Would send: {repr(sequence)}")
        return
    sys.stdout.write(osc.maybe_wrap_for_tmux(sequence))
    sys.stdout.flush()


@cli.command('start')
@click.option('--id', 'layer_id', help='Layer ID to start')
@click.option('--plugin', '-p', 'plugin_name', help='Start all layers of this plugin')
@click.pass_context
def cmd_start(ctx, layer_id, plugin_name):
    """Start (resume) layer(s)."""
    if not layer_id and not plugin_name:
        raise click.ClickException("Either --id or --plugin is required")
    sequence = osc.start_sequence(id=layer_id, plugin=plugin_name)
    if ctx.obj['dry_run']:
        click.echo(f"Would send: {repr(sequence)}")
        return
    sys.stdout.write(osc.maybe_wrap_for_tmux(sequence))
    sys.stdout.flush()


@cli.command('decode')
@click.argument('input_file', type=click.Path(exists=True))
@click.option('--output', '-o', type=click.Path(), help='Output file')
@click.option('--info', '-i', is_flag=True, help='Show metadata only')
@click.option('--raw', '-r', is_flag=True, help='Output raw bytes')
@click.pass_context
def cmd_decode(ctx, input_file, output, info, raw):
    """Decode a yetty OSC sequence file."""
    with open(input_file, 'r') as f:
        content = f.read().rstrip('\r\n')

    # Strip terminators
    if content.endswith('\x07'):
        content = content[:-1]
    elif content.endswith('\x1b\\'):
        content = content[:-2]

    # Strip OSC prefix
    if content.startswith('\x1b]'):
        content = content[2:]
    elif content.startswith(']'):
        content = content[1:]
    else:
        raise click.ClickException("Invalid OSC sequence")

    # Parse new format: vendor-id;generic-args;plugin-args;payload
    parts = content.split(';', 3)
    if len(parts) < 4:
        raise click.ClickException(f"Invalid OSC format: expected 4 fields, got {len(parts)}")

    vendor_id, generic_args, plugin_args, encoded_payload = parts

    if vendor_id != str(osc.VENDOR_ID):
        raise click.ClickException(f"Unknown vendor ID: {vendor_id}")

    if info or ctx.obj['verbose']:
        click.echo(f"Generic args: {generic_args}", err=True)
        click.echo(f"Plugin args:  {plugin_args or '(none)'}", err=True)
        click.echo(f"Payload:      {len(encoded_payload)} chars (base94)", err=True)

    if info:
        return

    try:
        decoded_bytes = base94.decode(encoded_payload)
        if not raw:
            decoded_text = decoded_bytes.decode('utf-8')
    except UnicodeDecodeError:
        if not raw:
            raise click.ClickException("Payload is not UTF-8. Use --raw for binary.")
    except Exception as e:
        raise click.ClickException(f"Decode failed: {e}")

    if raw:
        if output:
            with open(output, 'wb') as f:
                f.write(decoded_bytes)
            click.echo(f"Saved: {output} ({len(decoded_bytes)} bytes)", err=True)
        else:
            sys.stdout.buffer.write(decoded_bytes)
    else:
        if output:
            with open(output, 'w') as f:
                f.write(decoded_text)
            click.echo(f"Saved: {output}", err=True)
        else:
            click.echo(decoded_text)


def main():
    cli(obj={})


if __name__ == '__main__':
    main()
