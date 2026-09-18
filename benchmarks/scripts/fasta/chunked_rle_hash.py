#!/usr/bin/env python3
import sys

def fnv_update(h, data):
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xffffffff
    return h

def emit_run(base, count):
    if count == 0:
        return b""

    if count > 5:
        return (
            bytes([
                count & 0xff,
                (count >> 8) & 0xff,
                (count >> 16) & 0xff,
                (count >> 24) & 0xff,
            ])
            + bytes([base])
        )

    return bytes([base]) * count

def compress_chunk(chunk):
    out = bytearray()
    in_header = False
    prev = None
    count = 0

    for c in chunk:
        if in_header:
            if c == ord("\n") or c == ord("\r"):
                in_header = False
            continue

        if c == ord(">"):
            in_header = True
            continue

        if c == ord("\n") or c == ord("\r"):
            continue

        if ord("a") <= c <= ord("z"):
            c -= 32

        if c not in (ord("A"), ord("C"), ord("G"), ord("T"), ord("N")):
            continue

        if count == 0:
            prev = c
            count = 1
        elif c == prev:
            count += 1
        else:
            out.extend(emit_run(prev, count))
            prev = c
            count = 1

    out.extend(emit_run(prev, count))
    return bytes(out)

def main():
    if len(sys.argv) != 3:
        print("Usage:")
        print("  python3 chunked_rle_hash.py input.fa chunk_size")
        sys.exit(1)

    path = sys.argv[1]
    chunk_size = int(sys.argv[2])

    h = 2166136261
    input_bytes = 0
    compressed_bytes = 0
    chunks = 0

    with open(path, "rb") as f:
        while True:
            chunk = f.read(chunk_size)
            if not chunk:
                break

            chunks += 1
            input_bytes += len(chunk)

            comp = compress_chunk(chunk)
            compressed_bytes += len(comp)
            h = fnv_update(h, comp)

    print(f"chunks={chunks}")
    print(f"input_bytes={input_bytes}")
    print(f"compressed_bytes={compressed_bytes}")
    print(f"fnv1a=0x{h:08x}")

if __name__ == "__main__":
    main()