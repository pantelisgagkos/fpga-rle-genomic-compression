#!/usr/bin/env python3
import sys


BASES = set(b"ACGTN")


def emit_run(out, base, count):
    if count == 0:
        return

    if count > 5:
        out.extend(count.to_bytes(4, "little"))
        out.append(base)
    else:
        out.extend(bytes([base]) * count)


def compress_one_raw_chunk(raw):
    """
    Matches the FPGA behavior per DMA block:
    - each raw chunk is compressed independently
    - ignores FASTA header lines starting with >
    - ignores \\n and \\r
    - lowercase -> uppercase
    - keeps A/C/G/T/N
    """
    out = bytearray()

    in_header = False
    prev = None
    count = 0

    for c in raw:
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

        if c not in BASES:
            continue

        if prev is None:
            prev = c
            count = 1
        elif c == prev:
            count += 1
        else:
            emit_run(out, prev, count)
            prev = c
            count = 1

    emit_run(out, prev, count)
    return bytes(out)


def compress_file_chunked(path, chunk_size):
    result = bytearray()

    with open(path, "rb") as f:
        while True:
            raw = f.read(chunk_size)
            if not raw:
                break
            result.extend(compress_one_raw_chunk(raw))

    return bytes(result)


def main():
    if len(sys.argv) != 4:
        print("Usage:")
        print("  python3 compare_chunked_rle.py input.fa fpga_output.rle chunk_size")
        print("")
        print("Example:")
        print("  python3 compare_chunked_rle.py test_mixed_200k.fa out_mixed_200k_buf16.rle 16384")
        sys.exit(1)

    input_path = sys.argv[1]
    fpga_path = sys.argv[2]
    chunk_size = int(sys.argv[3])

    expected = compress_file_chunked(input_path, chunk_size)

    with open(fpga_path, "rb") as f:
        fpga = f.read()

    print(f"Expected compressed size: {len(expected)} bytes")
    print(f"FPGA output size        : {len(fpga)} bytes")

    if expected == fpga:
        print("PASS: FPGA output matches exact PC chunked RLE reference")
        return

    print("FAIL: FPGA output differs from exact PC chunked RLE reference")

    min_len = min(len(expected), len(fpga))

    for i in range(min_len):
        if expected[i] != fpga[i]:
            print(f"First byte mismatch at compressed offset {i}")
            print(f"Expected: 0x{expected[i]:02X}")
            print(f"FPGA    : 0x{fpga[i]:02X}")

            start = max(0, i - 16)
            end = min(min_len, i + 32)

            print("")
            print("Expected around mismatch:")
            print(expected[start:end].hex(" "))

            print("")
            print("FPGA around mismatch:")
            print(fpga[start:end].hex(" "))
            break

    if len(expected) != len(fpga):
        print("")
        print(f"Length mismatch: expected {len(expected)}, got {len(fpga)}")

    sys.exit(1)


if __name__ == "__main__":
    main()