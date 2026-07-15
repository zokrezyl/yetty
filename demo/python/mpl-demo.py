#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.10"
# dependencies = ["matplotlib", "numpy"]
# ///
"""matplotlib figures rendered inline in yetty — the cheap-path POC.

Run inside a yetty session (ycat on PATH or YETTY_YCAT set):

    ./demo/python/mpl-demo.py
"""

import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

# Resolve the bindings package: YETTY_REPO when staged elsewhere (the
# sci-tour recording copies this script into its staging cwd), else
# relative to this file in the source tree.
repo_root = os.environ.get(
    "YETTY_REPO", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(repo_root, "bindings", "python"))
from yetty import mpl as yetty_mpl

yetty_mpl.install()

print("a damped oscillator, straight from matplotlib:")
time_axis = np.linspace(0, 10, 400)
signal = np.exp(-time_axis / 4) * np.cos(2 * np.pi * time_axis)
figure, axes = plt.subplots(figsize=(8, 3.2), dpi=120)
axes.plot(time_axis, signal, label="x(t) = e^{-t/4} cos(2πt)")
axes.fill_between(time_axis, signal - 0.08, signal + 0.08, alpha=0.25)
axes.set_xlabel("t (s)")
axes.set_ylabel("x")
axes.legend()
axes.grid(alpha=0.3)
figure.tight_layout()
plt.show()

print("and a 2D field with a colorbar:")
grid_x, grid_y = np.meshgrid(np.linspace(-3, 3, 200), np.linspace(-2, 2, 140))
field = np.exp(-(grid_x**2 + grid_y**2)) * np.cos(3 * grid_x)
figure, axes = plt.subplots(figsize=(8, 3.2), dpi=120)
mesh = axes.pcolormesh(grid_x, grid_y, field, cmap="magma")
figure.colorbar(mesh, ax=axes, label="amplitude")
figure.tight_layout()
plt.show()

print("done — matplotlib output, one scrollback.")
