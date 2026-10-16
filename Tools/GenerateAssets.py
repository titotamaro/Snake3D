#!/usr/bin/env python3
"""Generates the Snake game's art and audio assets procedurally (deterministic output, no dependencies).

Writes into SnakeGame/Assets:
  Meshes/*.gltf + *.bin   glTF 2.0 models with metallic-roughness PBR materials
  Textures/*.png          base color / normal / roughness textures used by the models
  Audio/*.wav             sound effects and a music loop

Usage: python Tools/GenerateAssets.py [--output <Assets directory>]
"""

import argparse
import json
import math
import os
import random
import struct
import wave
import zlib

# ----------------------------------------------------------------------------------------------------------------------
# PNG writing


def write_png(path, width, height, pixels):
    """pixels: list of (r, g, b, a) tuples (0-255), row-major, top row first."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        for x in range(width):
            raw.extend(pixels[y * width + x])

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as file:
        file.write(png)


def clamp_byte(value):
    return max(0, min(255, int(round(value))))


# ----------------------------------------------------------------------------------------------------------------------
# Noise helpers


class ValueNoise:
    def __init__(self, seed, period):
        rng = random.Random(seed)
        self.period = period
        self.values = [[rng.random() for _ in range(period)] for _ in range(period)]

    def sample(self, x, y):
        x0, y0 = int(math.floor(x)), int(math.floor(y))
        fx, fy = x - x0, y - y0
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        p = self.period
        v00 = self.values[y0 % p][x0 % p]
        v10 = self.values[y0 % p][(x0 + 1) % p]
        v01 = self.values[(y0 + 1) % p][x0 % p]
        v11 = self.values[(y0 + 1) % p][(x0 + 1) % p]
        return (v00 * (1 - fx) + v10 * fx) * (1 - fy) + (v01 * (1 - fx) + v11 * fx) * fy

    def fbm(self, x, y, octaves=4):
        total, amplitude, frequency, norm = 0.0, 1.0, 1.0, 0.0
        for _ in range(octaves):
            total += self.sample(x * frequency, y * frequency) * amplitude
            norm += amplitude
            amplitude *= 0.5
            frequency *= 2.0
        return total / norm


def height_to_normal_map(heights, width, height, strength):
    pixels = []
    for y in range(height):
        for x in range(width):
            left = heights[y * width + (x - 1) % width]
            right = heights[y * width + (x + 1) % width]
            up = heights[((y - 1) % height) * width + x]
            down = heights[((y + 1) % height) * width + x]
            nx = (left - right) * strength
            ny = (up - down) * strength
            nz = 1.0
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            pixels.append((clamp_byte((nx / length * 0.5 + 0.5) * 255), clamp_byte((ny / length * 0.5 + 0.5) * 255),
                           clamp_byte((nz / length * 0.5 + 0.5) * 255), 255))
    return pixels


# ----------------------------------------------------------------------------------------------------------------------
# Textures


def generate_textures(directory):
    size = 128

    # Snake scales: hexagon-ish cell pattern from distance to a staggered grid of cells.
    scale_heights = []
    scale_colors = []
    noise = ValueNoise(7, 16)
    cells = 8
    for y in range(size):
        for x in range(size):
            u, v = x / size * cells, y / size * cells
            row = math.floor(v)
            shifted_u = u + (0.5 if row % 2 else 0.0)
            cell_u, cell_v = shifted_u - math.floor(shifted_u) - 0.5, v - row - 0.5
            distance = math.sqrt(cell_u * cell_u + cell_v * cell_v * 1.6)
            bump = max(0.0, 1.0 - distance * 1.9)
            scale_heights.append(bump)
            variation = noise.fbm(x / 16.0, y / 16.0)
            base = (52 + 50 * variation, 150 + 50 * bump + 20 * variation, 60 + 25 * variation)
            edge = 1.0 - bump
            scale_colors.append((clamp_byte(base[0] * (1 - 0.35 * edge)), clamp_byte(base[1] * (1 - 0.35 * edge)),
                                 clamp_byte(base[2] * (1 - 0.35 * edge)), 255))
    write_png(os.path.join(directory, "SnakeScales_BaseColor.png"), size, size, scale_colors)
    write_png(os.path.join(directory, "SnakeScales_Normal.png"), size, size, height_to_normal_map(scale_heights, size, size, 6.0))

    # Grass board: two-tone checker (one check per grid cell when the UVs span the board) with noise.
    grass = []
    noise = ValueNoise(11, 32)
    for y in range(size):
        for x in range(size):
            checker = ((x // (size // 2)) + (y // (size // 2))) % 2
            n = noise.fbm(x / 12.0, y / 12.0)
            tone = 1.0 if checker == 0 else 0.86
            grass.append((clamp_byte((70 + 40 * n) * tone), clamp_byte((150 + 45 * n) * tone), clamp_byte((60 + 25 * n) * tone), 255))
    write_png(os.path.join(directory, "Grass_BaseColor.png"), size, size, grass)

    # Stone wall: rounded bricks with mortar, plus normal and metallic-roughness maps.
    stone_colors, stone_heights, stone_mr = [], [], []
    noise = ValueNoise(23, 32)
    brick_w, brick_h = size // 4, size // 8
    for y in range(size):
        for x in range(size):
            row = y // brick_h
            offset = (brick_w // 2) if row % 2 else 0
            bx = (x + offset) % brick_w
            by = y % brick_h
            edge = min(bx, brick_w - 1 - bx, by, brick_h - 1 - by)
            mortar = edge < 2
            n = noise.fbm(x / 10.0, y / 10.0)
            if mortar:
                stone_colors.append((clamp_byte(70 + 20 * n), clamp_byte(66 + 20 * n), clamp_byte(60 + 20 * n), 255))
                stone_heights.append(0.0)
                stone_mr.append((255, 240, 0, 255))
            else:
                shade = 120 + 60 * n + ((row * 37) % 5) * 6
                stone_colors.append((clamp_byte(shade), clamp_byte(shade * 0.93), clamp_byte(shade * 0.85), 255))
                stone_heights.append(min(1.0, edge / 4.0) * (0.8 + 0.2 * n))
                stone_mr.append((255, clamp_byte(150 + 80 * n), 0, 255))
    write_png(os.path.join(directory, "Stone_BaseColor.png"), size, size, stone_colors)
    write_png(os.path.join(directory, "Stone_Normal.png"), size, size, height_to_normal_map(stone_heights, size, size, 3.0))
    write_png(os.path.join(directory, "Stone_MetallicRoughness.png"), size, size, stone_mr)


# ----------------------------------------------------------------------------------------------------------------------
# Geometry


def normalize(v):
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    return (v[0] / length, v[1] / length, v[2] / length) if length > 1e-9 else (0.0, 1.0, 0.0)


class Primitive:
    def __init__(self, material):
        self.material = material
        self.positions, self.normals, self.uvs, self.indices = [], [], [], []

    def add_vertex(self, position, normal, uv):
        self.positions.append(position)
        self.normals.append(normal)
        self.uvs.append(uv)
        return len(self.positions) - 1


def rounded_box(primitive, half_extents, radius, segments=6, center=(0.0, 0.0, 0.0), uv_scale=1.0):
    """Subdivided cube whose vertices are pushed onto a rounded box surface."""
    inner = tuple(max(h - radius, 0.0) for h in half_extents)
    faces = [
        ((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
        ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
        ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
    ]
    for normal, right, up in faces:
        base = len(primitive.positions)
        for j in range(segments + 1):
            for i in range(segments + 1):
                s, t = i / segments * 2 - 1, j / segments * 2 - 1
                p = [normal[k] * half_extents[k] + right[k] * s * half_extents[k] + up[k] * t * half_extents[k] for k in range(3)]
                clamped = [max(-inner[k], min(inner[k], p[k])) for k in range(3)]
                direction = normalize([p[k] - clamped[k] for k in range(3)])
                position = tuple(clamped[k] + direction[k] * radius + center[k] for k in range(3))
                primitive.add_vertex(position, direction, ((s * 0.5 + 0.5) * uv_scale, (0.5 - t * 0.5) * uv_scale))
        for j in range(segments):
            for i in range(segments):
                a = base + j * (segments + 1) + i
                b, c, d = a + 1, a + segments + 2, a + segments + 1
                primitive.indices += [a, b, c, a, c, d]


def ellipsoid(primitive, radii, center, segments=24, rings=14):
    base = len(primitive.positions)
    for ring in range(rings + 1):
        v = ring / rings
        theta = v * math.pi
        for segment in range(segments + 1):
            u = segment / segments
            phi = u * 2 * math.pi
            unit = (math.sin(theta) * math.cos(phi), math.cos(theta), -math.sin(theta) * math.sin(phi))
            position = tuple(center[k] + unit[k] * radii[k] for k in range(3))
            normal = normalize(tuple(unit[k] / radii[k] for k in range(3)))
            primitive.add_vertex(position, normal, (u, v))
    stride = segments + 1
    for ring in range(rings):
        for segment in range(segments):
            a = base + ring * stride + segment
            b = a + stride
            primitive.indices += [a, b, a + 1, a + 1, b, b + 1]


def cylinder(primitive, radius, height, center, segments=12):
    base = len(primitive.positions)
    for ring in range(2):
        y = center[1] + (ring - 0.5) * height
        for segment in range(segments + 1):
            phi = segment / segments * 2 * math.pi
            normal = (math.cos(phi), 0.0, -math.sin(phi))
            primitive.add_vertex((center[0] + normal[0] * radius, y, center[2] + normal[2] * radius), normal, (segment / segments, ring))
    stride = segments + 1
    for segment in range(segments):
        a = base + segment
        b = a + stride
        primitive.indices += [a, a + 1, b, a + 1, b + 1, b]


def plane(primitive, half_size, uv_repeat):
    corners = [(-1, 1), (1, 1), (1, -1), (-1, -1)]
    base = len(primitive.positions)
    for x, z in corners:
        primitive.add_vertex((x * half_size, 0.0, z * half_size), (0.0, 1.0, 0.0),
                             ((x * 0.5 + 0.5) * uv_repeat, (z * 0.5 + 0.5) * uv_repeat))
    primitive.indices += [base, base + 1, base + 2, base, base + 2, base + 3]


# ----------------------------------------------------------------------------------------------------------------------
# glTF writing


def write_gltf(directory, name, primitives, materials, images):
    """materials: list of dicts {name, baseColor, metallic, roughness, emissive?, baseColorTexture?, normalTexture?, mrTexture?}"""
    binary = bytearray()
    buffer_views, accessors = [], []

    def add_view(data, target):
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        binary.extend(data)
        buffer_views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        return len(buffer_views) - 1

    def add_accessor(view, component, count, kind, minimum=None, maximum=None):
        accessor = {"bufferView": view, "componentType": component, "count": count, "type": kind}
        if minimum is not None:
            accessor["min"], accessor["max"] = minimum, maximum
        accessors.append(accessor)
        return len(accessors) - 1

    gltf_primitives = []
    for primitive in primitives:
        count = len(primitive.positions)
        positions = b"".join(struct.pack("<3f", *p) for p in primitive.positions)
        normals = b"".join(struct.pack("<3f", *n) for n in primitive.normals)
        uvs = b"".join(struct.pack("<2f", *uv) for uv in primitive.uvs)
        indices = b"".join(struct.pack("<I", i) for i in primitive.indices)
        minimum = [min(p[k] for p in primitive.positions) for k in range(3)]
        maximum = [max(p[k] for p in primitive.positions) for k in range(3)]
        gltf_primitives.append({
            "attributes": {
                "POSITION": add_accessor(add_view(positions, 34962), 5126, count, "VEC3", minimum, maximum),
                "NORMAL": add_accessor(add_view(normals, 34962), 5126, count, "VEC3"),
                "TEXCOORD_0": add_accessor(add_view(uvs, 34962), 5126, count, "VEC2"),
            },
            "indices": add_accessor(add_view(indices, 34963), 5125, len(primitive.indices), "SCALAR"),
            "material": primitive.material,
            "mode": 4,
        })

    gltf_materials = []
    for material in materials:
        entry = {
            "name": material["name"],
            "pbrMetallicRoughness": {
                "baseColorFactor": material.get("baseColor", [1, 1, 1, 1]),
                "metallicFactor": material.get("metallic", 0.0),
                "roughnessFactor": material.get("roughness", 0.5),
            },
        }
        if "baseColorTexture" in material:
            entry["pbrMetallicRoughness"]["baseColorTexture"] = {"index": material["baseColorTexture"]}
        if "mrTexture" in material:
            entry["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {"index": material["mrTexture"]}
        if "normalTexture" in material:
            entry["normalTexture"] = {"index": material["normalTexture"], "scale": material.get("normalScale", 1.0)}
        if "emissive" in material:
            entry["emissiveFactor"] = material["emissive"]
        gltf_materials.append(entry)

    document = {
        "asset": {"version": "2.0", "generator": "Ouroboros GenerateAssets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": gltf_primitives}],
        "materials": gltf_materials,
        "buffers": [{"uri": name + ".bin", "byteLength": len(binary)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
    }
    if images:
        document["images"] = [{"uri": "../Textures/" + image} for image in images]
        document["samplers"] = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
        document["textures"] = [{"sampler": 0, "source": index} for index in range(len(images))]

    with open(os.path.join(directory, name + ".bin"), "wb") as file:
        file.write(binary)
    with open(os.path.join(directory, name + ".gltf"), "w", newline="\n") as file:
        json.dump(document, file, indent=1)
        file.write("\n")


def generate_meshes(directory):
    scale_images = ["SnakeScales_BaseColor.png", "SnakeScales_Normal.png"]

    # Snake head: rounded box with eyes.
    skin = Primitive(0)
    rounded_box(skin, (0.45, 0.38, 0.47), 0.2, segments=8)
    eye_white = Primitive(1)
    pupil = Primitive(2)
    for side in (-1, 1):
        ellipsoid(eye_white, (0.11, 0.11, 0.11), (side * 0.2, 0.3, -0.25), 16, 10)
        ellipsoid(pupil, (0.055, 0.06, 0.04), (side * 0.2, 0.33, -0.34), 12, 8)
    tongue = Primitive(3)
    rounded_box(tongue, (0.04, 0.015, 0.12), 0.012, segments=2, center=(0.0, -0.05, -0.55))
    write_gltf(directory, "SnakeHead", [skin, eye_white, pupil, tongue], [
        {"name": "SnakeSkin", "baseColor": [1, 1, 1, 1], "roughness": 0.45, "baseColorTexture": 0, "normalTexture": 1},
        {"name": "EyeWhite", "baseColor": [0.95, 0.95, 0.92, 1], "roughness": 0.15},
        {"name": "Pupil", "baseColor": [0.02, 0.02, 0.02, 1], "roughness": 0.1},
        {"name": "Tongue", "baseColor": [0.85, 0.12, 0.2, 1], "roughness": 0.4},
    ], scale_images)

    # Snake body segment
    body = Primitive(0)
    rounded_box(body, (0.4, 0.33, 0.4), 0.18, segments=8)
    write_gltf(directory, "SnakeBody", [body], [
        {"name": "SnakeSkin", "baseColor": [0.85, 1, 0.85, 1], "roughness": 0.5, "baseColorTexture": 0, "normalTexture": 1},
    ], scale_images)

    # Apple: red body, brown stem, green leaf
    apple = Primitive(0)
    ellipsoid(apple, (0.36, 0.33, 0.36), (0.0, 0.0, 0.0), 28, 18)
    stem = Primitive(1)
    cylinder(stem, 0.03, 0.2, (0.0, 0.38, 0.0), 10)
    leaf = Primitive(2)
    ellipsoid(leaf, (0.14, 0.025, 0.07), (0.12, 0.4, 0.0), 12, 6)
    write_gltf(directory, "Apple", [apple, stem, leaf], [
        {"name": "AppleSkin", "baseColor": [0.86, 0.08, 0.06, 1], "roughness": 0.25, "emissive": [0.08, 0.0, 0.0]},
        {"name": "Stem", "baseColor": [0.35, 0.2, 0.08, 1], "roughness": 0.8},
        {"name": "Leaf", "baseColor": [0.2, 0.65, 0.15, 1], "roughness": 0.5},
    ], [])

    # Board: a 20 x 20 plane with one checker texel pair per two cells
    board = Primitive(0)
    plane(board, 10.0, 10.0)
    write_gltf(directory, "Board", [board], [
        {"name": "Grass", "baseColor": [1, 1, 1, 1], "roughness": 0.9, "baseColorTexture": 0},
    ], ["Grass_BaseColor.png"])

    # Wall block: one grid cell, slightly rounded, stone textured
    wall = Primitive(0)
    rounded_box(wall, (0.5, 0.5, 0.5), 0.06, segments=4)
    write_gltf(directory, "Wall", [wall], [
        {"name": "Stone", "baseColor": [1, 1, 1, 1], "metallic": 1.0, "roughness": 1.0,
         "baseColorTexture": 0, "normalTexture": 1, "mrTexture": 2},
    ], ["Stone_BaseColor.png", "Stone_Normal.png", "Stone_MetallicRoughness.png"])


# ----------------------------------------------------------------------------------------------------------------------
# Audio


SAMPLE_RATE = 22050


def write_wav(path, samples):
    with wave.open(path, "wb") as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(SAMPLE_RATE)
        file.writeframes(b"".join(struct.pack("<h", max(-32767, min(32767, int(s * 32767)))) for s in samples))


def envelope(t, duration, attack=0.005, release=0.05):
    if t < attack:
        return t / attack
    if t > duration - release:
        return max(0.0, (duration - t) / release)
    return 1.0


def generate_audio(directory):
    rng = random.Random(3)

    # Eat: quick rising two-tone blip
    duration = 0.18
    samples = []
    for i in range(int(duration * SAMPLE_RATE)):
        t = i / SAMPLE_RATE
        frequency = 660 if t < 0.07 else 990
        samples.append(0.45 * math.sin(2 * math.pi * frequency * t) * envelope(t, duration))
    write_wav(os.path.join(directory, "Eat.wav"), samples)

    # Game over: descending tone with noise
    duration = 0.9
    samples = []
    phase = 0.0
    for i in range(int(duration * SAMPLE_RATE)):
        t = i / SAMPLE_RATE
        frequency = 440 * math.pow(0.25, t / duration)
        phase += 2 * math.pi * frequency / SAMPLE_RATE
        square = 1.0 if math.sin(phase) > 0 else -1.0
        noise = rng.uniform(-1, 1) * max(0.0, 0.4 - t)
        samples.append((0.25 * square + 0.2 * noise) * envelope(t, duration, release=0.3))
    write_wav(os.path.join(directory, "GameOver.wav"), samples)

    # Turn: short soft click
    duration = 0.04
    samples = [0.3 * math.sin(2 * math.pi * 1800 * (i / SAMPLE_RATE)) * envelope(i / SAMPLE_RATE, duration, release=0.03)
               for i in range(int(duration * SAMPLE_RATE))]
    write_wav(os.path.join(directory, "Turn.wav"), samples)

    # Music: a gentle 8-bar arpeggio loop (C - Am - F - G), seamless at the loop point
    tempo = 120.0
    step = 60.0 / tempo / 2  # eighth notes
    chords = [[60, 64, 67, 72], [57, 60, 64, 69], [53, 57, 60, 65], [55, 59, 62, 67]]
    notes = []
    for chord in chords:
        for repeat in range(2):
            pattern = [0, 1, 2, 3, 2, 1, 0, 1]
            notes += [chord[p] for p in pattern]
    total = int(len(notes) * step * SAMPLE_RATE)
    samples = [0.0] * total
    for index, note in enumerate(notes):
        frequency = 440.0 * math.pow(2.0, (note - 69) / 12.0)
        start = int(index * step * SAMPLE_RATE)
        length = int(step * SAMPLE_RATE * 1.8)
        for i in range(length):
            t = i / SAMPLE_RATE
            value = 0.12 * math.sin(2 * math.pi * frequency * t) * math.exp(-t * 6.0)
            value += 0.04 * math.sin(2 * math.pi * frequency * 2 * t) * math.exp(-t * 9.0)
            samples[(start + i) % total] += value
    # Bass on each chord root
    for index, chord in enumerate(chords * 2):
        frequency = 440.0 * math.pow(2.0, (chord[0] - 12 - 69) / 12.0)
        start = int(index * 4 * step * SAMPLE_RATE)
        for i in range(int(4 * step * SAMPLE_RATE)):
            t = i / SAMPLE_RATE
            samples[(start + i) % total] += 0.1 * math.sin(2 * math.pi * frequency * t) * min(1.0, t * 50) * math.exp(-t * 1.5)
    write_wav(os.path.join(directory, "Music.wav"), samples)


# ----------------------------------------------------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    default_output = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "SnakeGame", "Assets")
    parser.add_argument("--output", default=default_output, help="Assets directory to write into")
    arguments = parser.parse_args()

    output = os.path.abspath(arguments.output)
    for folder in ("Meshes", "Textures", "Audio"):
        os.makedirs(os.path.join(output, folder), exist_ok=True)

    generate_textures(os.path.join(output, "Textures"))
    generate_meshes(os.path.join(output, "Meshes"))
    generate_audio(os.path.join(output, "Audio"))
    print("Assets written to", output)


if __name__ == "__main__":
    main()
