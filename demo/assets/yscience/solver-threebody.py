#!/usr/bin/env python3
"""Three-body problem: integrate the Chenciner-Montgomery figure-8 orbit.

A real numerical experiment, small enough to run live inside the sci-tour
clip: three equal masses under Newtonian gravity, integrated with classic
RK4 over one full period. Progress lines (with the energy-conservation
check every physicist looks for) go to stdout; the computed trajectories
are written as a ychart scatter document.

    ./solver-threebody.py [output.json]     (default: threebody-orbit.json)

Initial conditions: Chenciner & Montgomery (2000), the stable figure-eight
choreography; G = 1, unit masses, period T = 6.32591398.
"""

import json
import sys
import time

BODY_COUNT = 3
PERIOD = 6.32591398
TIME_STEP = 0.0005
SAMPLE_EVERY = 40
LOG_EVERY = 2109  # six progress lines over one period


def accelerations(positions):
    result = [[0.0, 0.0] for _ in range(BODY_COUNT)]
    for first in range(BODY_COUNT):
        for second in range(first + 1, BODY_COUNT):
            delta_x = positions[second][0] - positions[first][0]
            delta_y = positions[second][1] - positions[first][1]
            distance_cubed = (delta_x * delta_x + delta_y * delta_y) ** 1.5
            result[first][0] += delta_x / distance_cubed
            result[first][1] += delta_y / distance_cubed
            result[second][0] -= delta_x / distance_cubed
            result[second][1] -= delta_y / distance_cubed
    return result


def total_energy(positions, velocities):
    kinetic = 0.5 * sum(vx * vx + vy * vy for vx, vy in velocities)
    potential = 0.0
    for first in range(BODY_COUNT):
        for second in range(first + 1, BODY_COUNT):
            delta_x = positions[second][0] - positions[first][0]
            delta_y = positions[second][1] - positions[first][1]
            potential -= 1.0 / (delta_x * delta_x + delta_y * delta_y) ** 0.5
    return kinetic + potential


def rk4_step(positions, velocities, step):
    def derivative(state_positions, state_velocities):
        return state_velocities, accelerations(state_positions)

    def advance(base, rates, factor):
        return [[base[body][0] + rates[body][0] * factor,
                 base[body][1] + rates[body][1] * factor]
                for body in range(BODY_COUNT)]

    velocity_k1, acceleration_k1 = derivative(positions, velocities)
    velocity_k2, acceleration_k2 = derivative(
        advance(positions, velocity_k1, step / 2),
        advance(velocities, acceleration_k1, step / 2))
    velocity_k3, acceleration_k3 = derivative(
        advance(positions, velocity_k2, step / 2),
        advance(velocities, acceleration_k2, step / 2))
    velocity_k4, acceleration_k4 = derivative(
        advance(positions, velocity_k3, step),
        advance(velocities, acceleration_k3, step))

    for body in range(BODY_COUNT):
        for axis in range(2):
            positions[body][axis] += step / 6 * (
                velocity_k1[body][axis] + 2 * velocity_k2[body][axis] +
                2 * velocity_k3[body][axis] + velocity_k4[body][axis])
            velocities[body][axis] += step / 6 * (
                acceleration_k1[body][axis] + 2 * acceleration_k2[body][axis] +
                2 * acceleration_k3[body][axis] + acceleration_k4[body][axis])


def stream_mode(sample_budget: int) -> int:
    """Live-telemetry mode: integrate continuously and print the body-1 /
    body-2 separation (a real dynamical observable that oscillates through
    the choreography) at ~30 Hz — pipe into yplot-stream for a live plot.
    Exits on its own after `sample_budget` samples so a piped consumer sees
    EOF and terminates cleanly (never leave an orphan producer streaming
    CMD_UPDATE envelopes at whatever figure holds its stream id next)."""
    positions = [[-0.97000436, 0.24308753],
                 [0.97000436, -0.24308753],
                 [0.0, 0.0]]
    velocities = [[0.4662036850, 0.4323657300],
                  [0.4662036850, 0.4323657300],
                  [-0.9324073700, -0.8647314600]]
    print(f"three-body solver: streaming body-1/body-2 separation at ~30 Hz")
    sys.stdout.flush()
    samples_emitted = 0
    step_index = 0
    while samples_emitted < sample_budget:
        rk4_step(positions, velocities, TIME_STEP)
        if step_index % 12 == 0:
            delta_x = positions[1][0] - positions[0][0]
            delta_y = positions[1][1] - positions[0][1]
            separation = (delta_x * delta_x + delta_y * delta_y) ** 0.5
            print(f"{separation:.5f}")
            sys.stdout.flush()
            samples_emitted += 1
            time.sleep(0.033)
        step_index += 1
    print("solver finished")
    return 0


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "--stream":
        sample_budget = int(sys.argv[2]) if len(sys.argv) > 2 else 400
        return stream_mode(sample_budget)
    output_path = sys.argv[1] if len(sys.argv) > 1 else "threebody-orbit.json"

    positions = [[-0.97000436, 0.24308753],
                 [0.97000436, -0.24308753],
                 [0.0, 0.0]]
    velocities = [[0.4662036850, 0.4323657300],
                  [0.4662036850, 0.4323657300],
                  [-0.9324073700, -0.8647314600]]

    step_count = int(PERIOD / TIME_STEP)
    initial_energy = total_energy(positions, velocities)
    trajectories = [[] for _ in range(BODY_COUNT)]

    print(f"three-body figure-8 | RK4 | dt={TIME_STEP} | "
          f"{step_count} steps over one period T={PERIOD}")
    print(f"E0 = {initial_energy:.9f} (must stay constant)")
    started = time.monotonic()

    for step_index in range(step_count + 1):
        if step_index % SAMPLE_EVERY == 0:
            for body in range(BODY_COUNT):
                trajectories[body].append(
                    {"x": round(positions[body][0], 4),
                     "y": round(positions[body][1], 4)})
        if step_index % LOG_EVERY == 0:
            energy = total_energy(positions, velocities)
            drift = abs((energy - initial_energy) / initial_energy)
            print(f"  t={step_index * TIME_STEP:6.3f}  E={energy:.9f}  "
                  f"|dE/E0|={drift:.2e}")
        rk4_step(positions, velocities, TIME_STEP)

    elapsed = time.monotonic() - started
    print(f"integrated {step_count} steps in {elapsed:.2f}s; "
          f"{len(trajectories[0])} samples per body")

    document = {
        "chart": "scatter",
        "title": "Three-body figure-8 choreography - RK4, one period",
        "x": "x",
        "y": "y",
        "series": [{"name": f"body {body + 1}",
                    "values": trajectories[body]}
                   for body in range(BODY_COUNT)],
    }
    with open(output_path, "w") as output_file:
        json.dump(document, output_file, separators=(",", ":"))
        output_file.write("\n")
    print(f"trajectories -> {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
