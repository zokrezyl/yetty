# A rotating six-fold mandala. Convert to polar, fold the angle into one
# mirrored wedge (so the pattern repeats six times around the circle), rotate
# the wedge with `time`, then sample fractal noise in the folded frame.
#
# Note on limits: the named locals are kept to a minimum (and sx/sy are folded
# straight into the fbm2 call) because a 5-octave fbm2 needs ~7 scratch
# registers of its own — leaving too many locals live would blow the
# 16-register budget. Reads x, y, time -> animated heatmap field.


def kaleido(x, y, time):
    radius = length2(x, y)
    wedge = tau / 6.0
    spin = abs(mod(atan2(y, x), wedge) - wedge * 0.5) + time * 0.2
    return fbm2(cos(spin) * radius * 1.6, sin(spin) * radius * 1.6, 5)
