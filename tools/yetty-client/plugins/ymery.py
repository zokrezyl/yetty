"""Ymery plugin - YAML-based ImGui widgets."""

import click


@click.command()
@click.option('--layouts-path', '-p', multiple=True, required=True, help='Layout search path (can specify multiple)')
@click.option('--main', '-m', required=True, help='Main module path (e.g., demo/layouts/simple/app.yaml)')
@click.pass_context
def ymery(ctx, layouts_path, main):
    """Ymery ImGui widget plugin.

    Renders YAML-defined ImGui interfaces in the terminal.

    Example:
        yetty-client create ymery -p demo/layouts -m demo/layouts/simple/app.yaml -w 40 -H 20
    """
    ctx.ensure_object(dict)

    # Build args string like: -p path1 -p path2 -m main_module
    args_parts = []
    for path in layouts_path:
        args_parts.append(f"-p {path}")
    args_parts.append(f"-m {main}")

    ctx.obj['plugin_args'] = ' '.join(args_parts)
    ctx.obj['payload'] = ''  # No payload needed, args are in plugin_args
    ctx.obj['plugin_name'] = 'ymery'
