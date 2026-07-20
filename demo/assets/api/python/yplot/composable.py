#!/usr/bin/env python3
"""yplot public API (Python) — composable Functions.

Instead of writing one big expression string, build each curve as its own
Function object, independently, and compose them into a Plot with
add_function(). Each Function owns its body, legend name and color, so different
curves can be constructed anywhere (a loop, a helper, another module) and only
brought together at the end.

    from yetty.api.yplot import Plot, Function
"""
from yetty.api.yplot import Plot, Function

# Build three harmonics independently. Function.create takes the body
# positionally; name and color are keyword arguments.
harmonics = [
    Function.create("sin(x)", name="first", color="#6BA892"),
    Function.create("sin(2*x)/2", name="second", color="#74C5A5"),
    Function.create("sin(3*x)/3", name="third", color="#5A8979"),
]

print("composing three harmonics with add_function()", flush=True)
plot = Plot.create(title="harmonic series", size=(600, 240), x_range=(-6.28, 6.28))
for harmonic in harmonics:
    plot.add_function(harmonic)
plot.show()

# add_function copies each Function into the plot, so the Functions can be
# released once composed.
for harmonic in harmonics:
    harmonic.destroy()
plot.destroy()

# A partial sum of the same harmonics, added alongside a named Function so the
# legend and color are explicit.
total = Function.create("sin(x)+sin(2*x)/2+sin(3*x)/3", name="sum", color="#FCBF49")
print("\nthe partial sum on its own", flush=True)
plot = Plot.create(title="square-wave approximation", size=(600, 240), x_range=(-6.28, 6.28))
plot.add_function(total)
plot.show()
total.destroy()
plot.destroy()
