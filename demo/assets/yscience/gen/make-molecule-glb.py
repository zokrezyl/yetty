#!/usr/bin/env python3
"""Build a ball-and-stick GLB from a PubChem 3D SDF file.

Emits exactly the mesh shape ymesh consumes today: one mesh, one primitive,
POSITION + NORMAL + uint16 indices (see src/yetty/ymesh/ymesh-glb.c). Atoms
become smooth icospheres, bonds become cylinders; everything is merged into
a single primitive.

    ./fetch-data.sh tmp/yscience-raw
    ./gen/make-molecule-glb.py tmp/yscience-raw/caffeine.sdf ../caffeine.glb

Pure stdlib on purpose: no numpy/trimesh dependency for a demo asset.
"""

import json
import math
import struct
import sys
from pathlib import Path

# Display radii in angstroms (ball-and-stick convention, not van der Waals).
ATOM_RADIUS = {"H": 0.24, "C": 0.36, "N": 0.34, "O": 0.34, "S": 0.40,
               "P": 0.40, "F": 0.30, "CL": 0.38}
DEFAULT_ATOM_RADIUS = 0.36
BOND_RADIUS = 0.11
BOND_SEGMENTS = 14
SPHERE_SUBDIVISIONS = 2


def parse_sdf(path: Path):
    """Read the first molecule of a V2000 SDF: atoms (element, xyz) + bonds."""
    lines = path.read_text().splitlines()
    counts_line = lines[3]
    atom_count = int(counts_line[0:3])
    bond_count = int(counts_line[3:6])
    atoms = []
    for atom_line in lines[4:4 + atom_count]:
        fields = atom_line.split()
        atoms.append((fields[3].upper(),
                      (float(fields[0]), float(fields[1]), float(fields[2]))))
    bonds = []
    for bond_line in lines[4 + atom_count:4 + atom_count + bond_count]:
        first_atom = int(bond_line[0:3]) - 1
        second_atom = int(bond_line[3:6]) - 1
        bonds.append((first_atom, second_atom))
    return atoms, bonds


def icosphere(subdivisions: int):
    """Unit icosphere with shared vertices; normals equal the positions."""
    golden = (1.0 + math.sqrt(5.0)) / 2.0
    base_vertices = [
        (-1, golden, 0), (1, golden, 0), (-1, -golden, 0), (1, -golden, 0),
        (0, -1, golden), (0, 1, golden), (0, -1, -golden), (0, 1, -golden),
        (golden, 0, -1), (golden, 0, 1), (-golden, 0, -1), (-golden, 0, 1),
    ]
    vertices = [normalize(vertex) for vertex in base_vertices]
    faces = [
        (0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
        (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
        (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
        (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1),
    ]
    midpoint_cache = {}

    def midpoint(index_a: int, index_b: int) -> int:
        key = (min(index_a, index_b), max(index_a, index_b))
        cached = midpoint_cache.get(key)
        if cached is not None:
            return cached
        vertex_a, vertex_b = vertices[index_a], vertices[index_b]
        middle = normalize(((vertex_a[0] + vertex_b[0]) / 2,
                            (vertex_a[1] + vertex_b[1]) / 2,
                            (vertex_a[2] + vertex_b[2]) / 2))
        vertices.append(middle)
        midpoint_cache[key] = len(vertices) - 1
        return len(vertices) - 1

    for _ in range(subdivisions):
        refined = []
        for corner_a, corner_b, corner_c in faces:
            mid_ab = midpoint(corner_a, corner_b)
            mid_bc = midpoint(corner_b, corner_c)
            mid_ca = midpoint(corner_c, corner_a)
            refined += [(corner_a, mid_ab, mid_ca), (corner_b, mid_bc, mid_ab),
                        (corner_c, mid_ca, mid_bc), (mid_ab, mid_bc, mid_ca)]
        faces = refined
    return vertices, faces


def normalize(vector):
    length = math.sqrt(sum(component * component for component in vector))
    return (vector[0] / length, vector[1] / length, vector[2] / length)


def cylinder_between(start, end, radius: float, segments: int):
    """Open-ended cylinder from start to end; smooth radial normals."""
    axis = (end[0] - start[0], end[1] - start[1], end[2] - start[2])
    axis_unit = normalize(axis)
    # A basis perpendicular to the axis.
    helper = (1.0, 0.0, 0.0) if abs(axis_unit[0]) < 0.9 else (0.0, 1.0, 0.0)
    side_u = normalize(cross(axis_unit, helper))
    side_v = cross(axis_unit, side_u)
    positions, normals, faces = [], [], []
    for segment in range(segments):
        angle = 2.0 * math.pi * segment / segments
        radial = tuple(side_u[axis_index] * math.cos(angle) +
                       side_v[axis_index] * math.sin(angle)
                       for axis_index in range(3))
        for cap_point in (start, end):
            positions.append(tuple(cap_point[axis_index] +
                                   radial[axis_index] * radius
                                   for axis_index in range(3)))
            normals.append(radial)
    for segment in range(segments):
        base = segment * 2
        base_next = ((segment + 1) % segments) * 2
        faces.append((base, base_next, base + 1))
        faces.append((base_next, base_next + 1, base + 1))
    return positions, normals, faces


def cross(vector_a, vector_b):
    return (vector_a[1] * vector_b[2] - vector_a[2] * vector_b[1],
            vector_a[2] * vector_b[0] - vector_a[0] * vector_b[2],
            vector_a[0] * vector_b[1] - vector_a[1] * vector_b[0])


def build_mesh(atoms, bonds):
    positions, normals, indices = [], [], []
    sphere_vertices, sphere_faces = icosphere(SPHERE_SUBDIVISIONS)

    # Center the molecule so the mesh orbits around its own centroid.
    centroid = tuple(sum(coordinates[axis_index] for _, coordinates in atoms)
                     / len(atoms) for axis_index in range(3))

    def centered(coordinates):
        return tuple(coordinates[axis_index] - centroid[axis_index]
                     for axis_index in range(3))

    for element, coordinates in atoms:
        radius = ATOM_RADIUS.get(element, DEFAULT_ATOM_RADIUS)
        center = centered(coordinates)
        vertex_base = len(positions)
        for unit_vertex in sphere_vertices:
            positions.append(tuple(center[axis_index] +
                                   unit_vertex[axis_index] * radius
                                   for axis_index in range(3)))
            normals.append(unit_vertex)
        for face in sphere_faces:
            indices.append(tuple(corner + vertex_base for corner in face))

    for first_atom, second_atom in bonds:
        start = centered(atoms[first_atom][1])
        end = centered(atoms[second_atom][1])
        vertex_base = len(positions)
        bond_positions, bond_normals, bond_faces = cylinder_between(
            start, end, BOND_RADIUS, BOND_SEGMENTS)
        positions += bond_positions
        normals += bond_normals
        for face in bond_faces:
            indices.append(tuple(corner + vertex_base for corner in face))

    return positions, normals, indices


def write_glb(output_path: Path, positions, normals, indices):
    if len(positions) > 0xFFFF:
        raise ValueError("mesh exceeds uint16 index range")
    index_blob = b"".join(struct.pack("<3H", *face) for face in indices)
    if len(index_blob) % 4:
        index_blob += b"\x00\x00"
    position_blob = b"".join(struct.pack("<3f", *point) for point in positions)
    normal_blob = b"".join(struct.pack("<3f", *vector) for vector in normals)
    binary_chunk = index_blob + position_blob + normal_blob

    bounds_min = [min(point[axis_index] for point in positions)
                  for axis_index in range(3)]
    bounds_max = [max(point[axis_index] for point in positions)
                  for axis_index in range(3)]
    document = {
        "asset": {"version": "2.0", "generator": "make-molecule-glb"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": 1, "NORMAL": 2},
            "indices": 0,
            "mode": 4,
        }]}],
        "buffers": [{"byteLength": len(binary_chunk)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0,
             "byteLength": len(index_blob), "target": 34963},
            {"buffer": 0, "byteOffset": len(index_blob),
             "byteLength": len(position_blob), "target": 34962},
            {"buffer": 0, "byteOffset": len(index_blob) + len(position_blob),
             "byteLength": len(normal_blob), "target": 34962},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5123,
             "count": len(indices) * 3, "type": "SCALAR"},
            {"bufferView": 1, "componentType": 5126,
             "count": len(positions), "type": "VEC3",
             "min": bounds_min, "max": bounds_max},
            {"bufferView": 2, "componentType": 5126,
             "count": len(normals), "type": "VEC3"},
        ],
    }
    json_chunk = json.dumps(document, separators=(",", ":")).encode()
    json_chunk += b" " * (-len(json_chunk) % 4)

    total_length = 12 + 8 + len(json_chunk) + 8 + len(binary_chunk)
    with open(output_path, "wb") as glb_file:
        glb_file.write(struct.pack("<3I", 0x46546C67, 2, total_length))
        glb_file.write(struct.pack("<2I", len(json_chunk), 0x4E4F534A))
        glb_file.write(json_chunk)
        glb_file.write(struct.pack("<2I", len(binary_chunk), 0x004E4942))
        glb_file.write(binary_chunk)
    print(f"wrote {output_path}: {len(positions)} vertices, "
          f"{len(indices)} triangles, {total_length} bytes")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    atoms, bonds = parse_sdf(Path(sys.argv[1]))
    positions, normals, indices = build_mesh(atoms, bonds)
    write_glb(Path(sys.argv[2]), positions, normals, indices)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
