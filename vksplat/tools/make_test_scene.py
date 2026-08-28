#!/usr/bin/env python3
"""Generate a 3D Gaussian Splatting .ply in the INRIA training output format."""

import argparse
import math
import os
import random
import struct
import sys

SH_C0 = 0.28209479177387814

def build_properties(sh_degree):
    rest = ((sh_degree + 1) ** 2 - 1) * 3
    return (
        ["x", "y", "z", "nx", "ny", "nz"]
        + [f"f_dc_{i}" for i in range(3)]
        + [f"f_rest_{i}" for i in range(rest)]
        + ["opacity"]
        + [f"scale_{i}" for i in range(3)]
        + [f"rot_{i}" for i in range(4)]
    )


def inverse_sigmoid(value):
    clamped = min(max(value, 1e-6), 1.0 - 1e-6)
    return math.log(clamped / (1.0 - clamped))


def colour_to_dc(channel):
    return (channel - 0.5) / SH_C0


def spiral_galaxy(count, rng):
    for index in range(count):
        t = index / max(count - 1, 1)
        angle = t * math.tau * 3.0
        radius = 0.2 + t * 2.0
        wobble = rng.gauss(0.0, 0.08)

        x = math.cos(angle) * radius + wobble
        y = rng.gauss(0.0, 0.12) * (1.0 - t * 0.6)
        z = math.sin(angle) * radius + wobble

        heat = 1.0 - t
        colour = (0.25 + heat * 0.75, 0.35 + heat * 0.4, 0.9 - heat * 0.3)
        scale = 0.012 + t * 0.02
        opacity = 0.55 + heat * 0.35

        axis_angle = rng.uniform(0.0, math.tau)
        rotation = (math.cos(axis_angle * 0.5), 0.0, math.sin(axis_angle * 0.5), 0.0)

        yield x, y, z, colour, scale, opacity, rotation


def write_ply(path, count, seed, sh_degree):
    rng = random.Random(seed)
    properties = build_properties(sh_degree)
    rest_count = len(properties) - 17

    header = [
        "ply",
        "format binary_little_endian 1.0",
        f"element vertex {count}",
    ]
    header += [f"property float {name}" for name in properties]
    header.append("end_header")

    directory = os.path.dirname(os.path.abspath(path))
    os.makedirs(directory, exist_ok=True)

    with open(path, "wb") as handle:
        handle.write(("\n".join(header) + "\n").encode("ascii"))
        for x, y, z, colour, scale, opacity, rotation in spiral_galaxy(count, rng):
            values = [
                x, y, z,
                0.0, 0.0, 0.0,
                colour_to_dc(colour[0]), colour_to_dc(colour[1]), colour_to_dc(colour[2]),
                *[rng.gauss(0.0, 0.05) for _ in range(rest_count)],
                inverse_sigmoid(opacity),
                math.log(scale), math.log(scale), math.log(scale * 0.6),
                rotation[0], rotation[1], rotation[2], rotation[3],
            ]
            handle.write(struct.pack(f"<{len(values)}f", *values))

    return len(properties) * 4 * count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output")
    parser.add_argument("--count", type=int, default=200000)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--sh-degree", type=int, default=0, choices=[0, 1, 2, 3])
    arguments = parser.parse_args()

    payload = write_ply(arguments.output, arguments.count, arguments.seed, arguments.sh_degree)
    print(f"wrote {arguments.output}: {arguments.count} splats, {payload / 1048576:.1f} MiB payload")
    return 0


if __name__ == "__main__":
    sys.exit(main())
