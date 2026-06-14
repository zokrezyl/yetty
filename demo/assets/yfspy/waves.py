# yfspy line-plot shaders. Each top-level def becomes one curve. Parameters
# bind positionally to [x, y, time, s0..s7]; these read only x (and time), so
# they render as ordinary line plots.


def wave(x, time):
    return sin(x * 3.0 + time) * 0.5 + cos(x * 2.0) * 0.3


def envelope(x):
    return exp(-x * x * 0.2) * sin(x * 6.0)
