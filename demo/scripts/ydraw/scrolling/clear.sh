#!/bin/bash
# YDraw Scrolling Layer: Clear
# Removes all drawings from the scrolling layer (OSC 666674)

printf '\033]600000;;\033\\'
echo "YDraw scrolling layer cleared"
