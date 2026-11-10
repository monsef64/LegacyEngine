"""TEMPORARY (test run of the release workflow).

shot.py <Xvfb framebuffer file> <output>
Prints a colour census of the screen and writes its RGB pixels, zlib-compressed.
"""
import collections
import struct
import sys
import zlib


def read_xwd(path):
    data = open(path, "rb").read()
    header = struct.unpack(">25I", data[:100])
    header_size, width, height = header[0], header[4], header[5]
    bits_per_pixel, bytes_per_line, ncolors = header[11], header[12], header[19]
    if bits_per_pixel != 32:
        raise SystemExit(f"unexpected bits_per_pixel {bits_per_pixel}")
    offset = header_size + ncolors * 12
    rows = []
    for y in range(height):
        row = data[offset + y * bytes_per_line: offset + y * bytes_per_line + width * 4]
        rgb = bytearray(width * 3)
        rgb[0::3] = row[2::4]
        rgb[1::3] = row[1::4]
        rgb[2::3] = row[0::4]
        rows.append(bytes(rgb))
    return width, height, rows


def census(width, height, rows):
    count = collections.Counter()
    box = {}
    for y, row in enumerate(rows):
        for x in range(width):
            colour = row[3 * x: 3 * x + 3]
            count[colour] += 1
            b = box.get(colour)
            if b is None:
                box[colour] = [x, y, x, y]
            else:
                if x < b[0]:
                    b[0] = x
                if x > b[2]:
                    b[2] = x
                b[3] = y
    lines = [f"{width}x{height} distinct={len(count)}"]
    for colour, n in count.most_common(12):
        b = box[colour]
        lines.append(f"{tuple(colour)} n={n} box={b[0]},{b[1]}-{b[2]},{b[3]}")
    return "\n".join(lines)


if __name__ == "__main__":
    width, height, rows = read_xwd(sys.argv[1])
    print(census(width, height, rows))
    packed = zlib.compress(b"".join(rows), 9)
    open(sys.argv[2], "wb").write(packed)
    print(f"zlib bytes={len(packed)}")
