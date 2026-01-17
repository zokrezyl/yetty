"""PDFium plugin - display PDF documents in terminal (BSD-3-Clause licensed)."""

import click
from pathlib import Path


@click.command()
@click.argument('file', type=click.Path(exists=True))
@click.option('--page', '-p', type=int, default=1, help='Initial page to display (1-indexed)')
@click.pass_context
def pdfium(ctx, file, page):
    """PDF viewer plugin using PDFium (BSD-3-Clause license).

    Renders PDF documents using PDFium and MSDF text rendering.
    Text is extracted from the PDF and rendered using the terminal font.

    This plugin uses PDFium (BSD-3-Clause licensed) which is MIT-compatible,
    unlike the 'pdf' plugin which uses MuPDF (AGPL licensed).

    Navigation:
        PageUp/PageDown - Previous/next page
        +/-             - Zoom in/out
        Mouse scroll    - Scroll within page
        Ctrl+scroll     - Zoom in/out

    Examples:
        yetty-client create pdfium document.pdf
        yetty-client create pdfium --page 5 manual.pdf
        yetty-client create pdfium -w 80 -H 40 report.pdf
    """
    ctx.ensure_object(dict)

    path = Path(file)
    if not path.exists():
        raise click.ClickException(f"File not found: {file}")

    if not path.suffix.lower() == '.pdf':
        raise click.ClickException(f"File must be a PDF: {file}")

    # Payload is just the file path - the C++ plugin handles everything
    payload = str(path.absolute())

    ctx.obj['payload'] = payload
    ctx.obj['plugin_name'] = 'pdfium'
    # Store page for potential future use (plugin would need to support it)
    ctx.obj['initial_page'] = page - 1  # Convert to 0-indexed
