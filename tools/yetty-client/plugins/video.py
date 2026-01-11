"""Video plugin - display videos in terminal."""

import sys
import os
import click
from pathlib import Path

# Supported video extensions
VIDEO_EXTENSIONS = {
    '.mp4', '.m4v', '.mov',  # MP4/QuickTime
    '.webm', '.mkv',          # WebM/Matroska
    '.avi',                   # AVI
    '.flv',                   # Flash Video
    '.gif',                   # Animated GIF
    '.mpg', '.mpeg',          # MPEG
    '.ts', '.mts',            # MPEG-TS
    '.ogv', '.ogg',           # Ogg/Theora
}


@click.command()
@click.argument('file', type=click.Path(exists=True), required=False)
@click.option('--input', '-i', 'input_', help='Video file path')
@click.option('--loop/--no-loop', default=True, help='Loop video playback')
@click.pass_context
def video(ctx, file, input_, loop):
    """Video player plugin.

    Displays videos in the terminal using FFmpeg decoding and WebGPU rendering.
    Supports MP4, WebM, MKV, AVI, GIF, and other common formats.

    Controls:
        Click       - Play/pause toggle

    Examples:
        yetty-client run video movie.mp4
        yetty-client run video -i animation.gif
    """
    ctx.ensure_object(dict)

    # Handle both positional argument and --input option
    source = file or input_
    if not source:
        raise click.ClickException("Please provide a video file path")

    path = Path(source)
    if not path.exists():
        raise click.ClickException(f"File not found: {source}")

    ext = path.suffix.lower()
    if ext not in VIDEO_EXTENSIONS:
        click.echo(f"Warning: Unknown extension '{ext}', attempting anyway", err=True)

    # Send absolute file path, not the video content
    abs_path = str(path.resolve())
    
    ctx.obj['payload'] = abs_path
    ctx.obj['plugin_name'] = 'video'
    ctx.obj['loop'] = loop
