# A travelling Gaussian wave packet: a carrier sine moving with `time` under a
# stationary Gaussian envelope. Reads x and time -> an animated line plot.


def pulse(x, time):
    envelope = exp(-x * x * 0.5)
    return envelope * sin(x * 8.0 - time * 3.0)
