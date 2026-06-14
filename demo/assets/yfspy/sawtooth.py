# Sawtooth wave as its Fourier series, summed in an in-shader while-loop with
# an alternating sign:  sum_k (-1)^(k+1) sin(k x) / k. The sign flip each
# iteration is plain control flow, not a closed-form expression.


def sawtooth(x):
    total = 0.0
    k = 1.0
    sign = 1.0
    while k < 12.0:
        total = total + sign * sin(k * x) / k
        sign = -sign
        k = k + 1.0
    return total * 0.5
