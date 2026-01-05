"""ThorVG plugin - vector graphics (SVG, Lottie animations, YAML VG)."""

import sys
import json
import click
from pathlib import Path


@click.command()
@click.option('--svg', '-s', 'svg_file', type=click.Path(exists=True), help='SVG file to display')
@click.option('--lottie', '-l', 'lottie_file', type=click.Path(exists=True), help='Lottie JSON file to display')
@click.option('--yaml', '-y', 'yaml_file', type=click.Path(exists=True), help='YAML vector graphics file')
@click.option('--input', '-i', 'input_file', type=click.Path(exists=True), help='Auto-detect file type')
@click.pass_context
def thorvg(ctx, svg_file, lottie_file, yaml_file, input_file):
    """ThorVG vector graphics plugin.

    Display SVG images, Lottie animations, or YAML-defined vector graphics.
    
    Supported formats:
      - SVG: Scalable Vector Graphics files
      - Lottie: JSON-based animations (from After Effects, etc.)
      - YAML: Custom vector graphics definition format

    Examples:
        yetty-client create thorvg --svg icon.svg -w 20 -H 20
        yetty-client create thorvg --lottie animation.json -w 60 -H 30
        yetty-client create thorvg --yaml shapes.yaml -w 40 -H 25
        yetty-client create thorvg -i auto-detect.svg
    """
    ctx.ensure_object(dict)

    # Count how many source options were provided
    sources = [svg_file, lottie_file, yaml_file, input_file]
    provided = sum(1 for s in sources if s is not None)
    
    if provided == 0:
        raise click.ClickException(
            "One of --svg, --lottie, --yaml, or --input is required"
        )
    if provided > 1:
        raise click.ClickException(
            "Only one source file option can be specified"
        )

    # Determine content type and read file
    content = None
    content_type = None
    
    if svg_file:
        content = Path(svg_file).read_text()
        content_type = 'svg'
    elif lottie_file:
        content = Path(lottie_file).read_text()
        content_type = 'lottie'
    elif yaml_file:
        content = Path(yaml_file).read_text()
        content_type = 'yaml'
    elif input_file:
        file_path = Path(input_file)
        content = file_path.read_text()
        
        # Auto-detect based on extension or content
        suffix = file_path.suffix.lower()
        if suffix == '.svg':
            content_type = 'svg'
        elif suffix in ('.json', '.lottie'):
            content_type = 'lottie'
        elif suffix in ('.yaml', '.yml'):
            content_type = 'yaml'
        else:
            # Try to detect from content
            stripped = content.strip()
            if stripped.startswith('<') or '<?xml' in stripped[:100]:
                content_type = 'svg'
            elif stripped.startswith('{'):
                content_type = 'lottie'
            else:
                content_type = 'yaml'

    # Build payload with type prefix for the C++ plugin to parse
    # Format: TYPE\n<content>
    payload = f"{content_type}\n{content}"
    
    ctx.obj['payload'] = payload
    ctx.obj['plugin_name'] = 'thorvg'
