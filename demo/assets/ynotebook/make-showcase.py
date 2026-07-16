#!/usr/bin/env python3
"""Generate showcase.ipynb — a Jupyter notebook whose outputs embed rich MIME
representations built from the shared demo assets (PNG raster, SVG vector, HTML,
markdown, JSON). ynb-cat renders each output as a real figure, so this is the
visual showcase for the ynotebook stack.

Run from anywhere; paths resolve relative to this file:

    ./demo/assets/ynotebook/make-showcase.py
"""

import base64
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.dirname(HERE)


def b64(rel_path):
    with open(os.path.join(ASSETS, rel_path), "rb") as handle:
        return base64.b64encode(handle.read()).decode("ascii")


def text_asset(rel_path):
    with open(os.path.join(ASSETS, rel_path), "r", encoding="utf-8") as handle:
        return handle.read()


def md_cell(cell_id, source):
    return {
        "cell_type": "markdown",
        "id": cell_id,
        "metadata": {},
        "source": source,
    }


def code_cell(cell_id, execution_count, source, outputs):
    return {
        "cell_type": "code",
        "id": cell_id,
        "execution_count": execution_count,
        "metadata": {},
        "source": source,
        "outputs": outputs,
    }


def display_data(data, metadata=None):
    return {"output_type": "display_data", "data": data, "metadata": metadata or {}}


def execute_result(execution_count, data, metadata=None):
    return {
        "output_type": "execute_result",
        "execution_count": execution_count,
        "data": data,
        "metadata": metadata or {},
    }


def stream(name, text):
    return {"output_type": "stream", "name": name, "text": text}


def error(ename, evalue, traceback):
    return {
        "output_type": "error",
        "ename": ename,
        "evalue": evalue,
        "traceback": traceback,
    }


HTML_TABLE = (
    "<table>\n"
    "  <thead><tr><th>region</th><th>share</th></tr></thead>\n"
    "  <tbody>\n"
    "    <tr><td>EMEA</td><td>41%</td></tr>\n"
    "    <tr><td>AMER</td><td>37%</td></tr>\n"
    "    <tr><td>APAC</td><td>22%</td></tr>\n"
    "  </tbody>\n"
    "</table>"
)

MARKDOWN_OUT = (
    "### Summary\n\n"
    "- **rows processed:** 1,204\n"
    "- **anomalies:** 3\n"
    "- status: `ok`\n"
)

JSON_OUT = {
    "model": "linear",
    "r2": 0.983,
    "coefficients": [1.02, -0.34, 0.07],
    "converged": True,
}

cells = [
    md_cell(
        "intro",
        "# yetty notebook showcase\n\n"
        "This notebook's **outputs** carry rich MIME bundles — raster images, "
        "vector graphics, HTML, markdown and JSON. `ynb-cat` renders each as a "
        "figure inline.",
    ),
    code_cell(
        "raster",
        1,
        "from PIL import Image\nImage.open('rose.png')",
        [
            execute_result(
                1,
                {
                    "image/png": b64("yimage/rose.png"),
                    "text/plain": "<PIL.PngImagePlugin.PngImageFile image mode=RGB size=64x64>",
                },
            )
        ],
    ),
    code_cell(
        "vector",
        2,
        "from IPython.display import SVG\nSVG('tiger.svg')",
        [
            display_data(
                {
                    "image/svg+xml": text_asset("svg/tiger.svg"),
                    "text/plain": "<IPython.core.display.SVG object>",
                }
            )
        ],
    ),
    code_cell(
        "plot",
        3,
        "import matplotlib.pyplot as plt\nplt.plot(data)\nplt.show()",
        [display_data({"image/png": b64("yimage/gradient.png")})],
    ),
    code_cell(
        "html",
        4,
        "import pandas as pd\ndf.to_html()",
        [
            execute_result(
                4,
                {
                    "text/html": HTML_TABLE,
                    "text/plain": "   region share\n0    EMEA   41%\n1    AMER   37%\n2    APAC   22%",
                },
            )
        ],
    ),
    code_cell(
        "markdown-out",
        5,
        "from IPython.display import Markdown\nMarkdown(report)",
        [display_data({"text/markdown": MARKDOWN_OUT})],
    ),
    code_cell(
        "stream-and-result",
        6,
        "print('hello from the kernel')\n6 * 7",
        [
            stream("stdout", "hello from the kernel\n"),
            execute_result(6, {"text/plain": "42"}),
        ],
    ),
    code_cell(
        "json-out",
        7,
        "fit_model(data)",
        [execute_result(7, {"application/json": JSON_OUT})],
    ),
    code_cell(
        "boom",
        8,
        "1 / 0",
        [
            error(
                "ZeroDivisionError",
                "division by zero",
                [
                    "Traceback (most recent call last):",
                    "  File \"<stdin>\", line 1, in <module>",
                    "ZeroDivisionError: division by zero",
                ],
            )
        ],
    ),
]

notebook = {
    "nbformat": 4,
    "nbformat_minor": 5,
    "metadata": {
        "kernelspec": {"display_name": "Python 3", "language": "python", "name": "python3"},
        "language_info": {"name": "python", "version": "3.11"},
    },
    "cells": cells,
}

out_path = os.path.join(HERE, "showcase.ipynb")
with open(out_path, "w", encoding="utf-8") as handle:
    json.dump(notebook, handle, indent=1)
    handle.write("\n")

print(f"wrote {out_path} ({len(cells)} cells)")
