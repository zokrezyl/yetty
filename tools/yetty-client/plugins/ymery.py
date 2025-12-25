"""Ymery plugin - YAML-based ImGui widgets."""

import click


@click.command()
@click.option('--layouts-path', '-p', required=True, help='Layout search path')
@click.option('--main', '-m', default='app', help='Main module name')
@click.option('--plugins', default='', help='Plugin search path')
@click.pass_context
def ymery(ctx, layouts_path, main, plugins):
    """Ymery ImGui widget plugin.

    Renders YAML-defined ImGui interfaces in the terminal.

    Example:
        yetty-client run ymery -p /path/to/layouts --plugins /path/to/plugins -m app
    """
    ctx.ensure_object(dict)

    parts = [f"layout_path={layouts_path}"]
    if plugins:
        parts.append(f"plugins={plugins}")
    parts.append(f"main={main}")

    ctx.obj['payload'] = ';'.join(parts)
    ctx.obj['plugin_name'] = 'ymery'
