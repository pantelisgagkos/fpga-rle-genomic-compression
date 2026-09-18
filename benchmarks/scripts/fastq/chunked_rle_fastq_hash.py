#!/usr/bin/env python3
import sys

def fnv_update(h, data):
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xffffffff
    return h

def emit_run(ch, count):
    if count == 0:
        return b""

    if count > 5:
        return bytes([
            count & 0xff,
            (count >> 8) & 0xff,
            (count >> 16) & 0xff,
            (count >> 24) & 0xff,
            ch
        ])

    return bytes([ch]) * count

def compress_rle_bytes(data):
    out = bytearray()
    prev = None
    count = 0

    for ch in data:
        if count == 0:
            prev = ch
            count = 1
        elif ch == prev:
            count += 1
        else:
            out.extend(emit_run(prev, count))
            prev = ch
            count = 1

    out.extend(emit_run(prev, count))
    return bytes(out)

def normalize_seq_line(line):
    out = bytearray()

    for c in line.rstrip(b"\r\n"):
        if 97 <= c <= 122:
            c -= 32

        if c in (ord("A"), ord("C"), ord("G"), ord("T"), ord("N")):
            out.append(c)

    return bytes(out)

def compress_fastq_chunk(chunk):
    out = bytearray()
    lines = chunk.splitlines()

    state = 0
    # 0: expect @header
    # 1: sequence
    # 2: plus
    # 3: quality

    for line in lines:
        if state == 0:
            if line.startswith(b"@"):
                state = 1

        elif state == 1:
            seq = normalize_seq_line(line)
            out.extend(compress_rle_bytes(seq))
            state = 2

        elif state == 2:
            if line.startswith(b"+"):
                state = 3

        elif state == 3:
            qual = line.rstrip(b"\r\n")
            out.extend(compress_rle_bytes(qual))
            state = 0

    return bytes(out)

def record_aligned_chunks(path, target_size):
    buf = bytearray()
    fastq_state = 0

    with open(path, "rb") as f:
        while True:
            data = f.read(4096)
            if not data:
                break

            for b in data:
                buf.append(b)

                if b == ord("\n"):
                    fastq_state += 1
                    if fastq_state >= 4:
                        fastq_state = 0

                    if len(buf) >= target_size and fastq_state == 0:
                        yield bytes(buf)
                        buf.clear()

        if buf:
            yield bytes(buf)

def main():
    if len(sys.argv) != 3:
        print("Usage:")
        print("  python3 chunked_rle_fastq_hash.py input.fastq target_chunk_size")
        sys.exit(1)

    path = sys.argv[1]
    target_size = int(sys.argv[2])

    h = 2166136261
    input_bytes = 0
    compressed_bytes = 0
    chunks = 0

    for chunk in record_aligned_chunks(path, target_size):
        chunks += 1
        input_bytes += len(chunk)

        comp = compress_fastq_chunk(chunk)
        compressed_bytes += len(comp)
        h = fnv_update(h, comp)

    print(f"chunks={chunks}")
    print(f"input_bytes={input_bytes}")
    print(f"compressed_bytes={compressed_bytes}")
    print(f"fnv1a=0x{h:08x}")

if __name__ == "__main__":
    main()
