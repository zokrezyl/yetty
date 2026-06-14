# Interference of three circular wave sources, animated by `time`. Each source
# emits an outgoing ring sin(distance * frequency - time * speed); the sum is
# normalized into [0,1]. Reads x, y -> heatmap field; reads time -> animated.


def ripples(x, y, time):
    a = sin(dist2(x, y, -1.0, -0.6) * 6.0 - time * 2.0)
    b = sin(dist2(x, y, 1.2, 0.3) * 7.0 - time * 1.5)
    c = sin(dist2(x, y, 0.0, 1.1) * 5.0 - time * 1.0)
    return (a + b + c) / 3.0 * 0.5 + 0.5
