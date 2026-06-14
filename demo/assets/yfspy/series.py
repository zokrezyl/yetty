# A partial Fourier sum computed with a real while-loop in the shader — the
# capability yfsvm control flow unlocks. This approximates a square wave by
# summing sin(k*x)/k over odd k, accumulating in a loop rather than a fixed
# unrolled expression.


def square_wave(x):
    total = 0.0
    k = 1.0
    while k < 16.0:
        total = total + sin(k * x) / k
        k = k + 2.0
    return total * 0.6
