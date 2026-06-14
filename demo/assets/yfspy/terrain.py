# A ridged-noise terrain silhouette. ridge2 sums ridged value-noise octaves;
# sampling along y = 0 turns the 2D field into a 1D mountain profile. Reads
# only x -> a line plot.


def terrain(x):
    return ridge2(x, 0.0, 5) * 1.4 - 0.5
