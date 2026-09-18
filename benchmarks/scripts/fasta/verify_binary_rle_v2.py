#!/usr/bin/env python3
import sys


BASES = set(b"ACGTN")


def normalize_fasta_sequence(path):
    seq = bytearray()
    in_header = False

    with open(path, "rb") as f:
        data = f.read()

    for c in data:
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

        if c in BASES:
            seq.append(c)

    return bytes(seq)


def read_u32_le(buf, i):
    return (
        buf[i]
        | (buf[i + 1] << 8)
        | (buf[i + 2] << 16)
        | (buf[i + 3] << 24)
    )


def compute_run_lengths(seq):
    run = [0] * len(seq)

    i = len(seq) - 1
    while i >= 0:
        if i == len(seq) - 1:
            run[i] = 1
        else:
            if seq[i] == seq[i + 1]:
                run[i] = run[i + 1] + 1
            else:
                run[i] = 1
        i -= 1

    return run


def verify_rle_against_expected(rle_data, expected):
    """
    Expected-aware verifier for the FPGA binary RLE format.

    The raw format is:
      - count > 5: 4-byte little-endian count + base
      - count <= 5: literal base bytes

    Because encoded tokens have no explicit marker, some byte patterns can be
    ambiguous. This verifier uses the original normalized FASTA sequence to
    choose the valid interpretation.
    """

    run_len = compute_run_lengths(expected)

    i = 0  # compressed offset
    j = 0  # expected sequence offset

    stack = []
    failed = set()

    while True:
        if i == len(rle_data) and j == len(expected):
            return True, i, j

        if i > len(rle_data) or j > len(expected):
            if not stack:
                return False, i, j
            i, j = stack.pop()
            continue

        state = (i, j)
        if state in failed:
            if not stack:
                return False, i, j
            i, j = stack.pop()
            continue

        candidates = []

        # Candidate 1: encoded run token
        if i + 4 < len(rle_data) and j < len(expected):
            count = read_u32_le(rle_data, i)
            base = rle_data[i + 4]

            if (
                count > 5
                and base in BASES
                and j + count <= len(expected)
                and expected[j] == base
                and run_len[j] >= count
            ):
                candidates.append((i + 5, j + count, "encoded"))

        # Candidate 2: literal byte
        if i < len(rle_data) and j < len(expected):
            c = rle_data[i]
            if c in BASES and c == expected[j]:
                candidates.append((i + 1, j + 1, "literal"))

        if not candidates:
            failed.add(state)
            if not stack:
                return False, i, j
            i, j = stack.pop()
            continue

        # If both interpretations are possible, keep the second as backup.
        # Prefer encoded first, because real long runs use encoded tokens.
        if len(candidates) > 1:
            for alt in candidates[1:]:
                stack.append((alt[0], alt[1]))

        i, j = candidates[0][0], candidates[0][1]


def main():
    if len(sys.argv) != 3:
        print("Usage:")
        print("  python3 verify_binary_rle_v2.py input.fa output.rle")
        sys.exit(1)

    fasta_path = sys.argv[1]
    rle_path = sys.argv[2]

    expected = normalize_fasta_sequence(fasta_path)

    with open(rle_path, "rb") as f:
        rle_data = f.read()

    ok, comp_pos, seq_pos = verify_rle_against_expected(rle_data, expected)

    print(f"FASTA normalized length: {len(expected)} bytes")
    print(f"RLE compressed size    : {len(rle_data)} bytes")

    if ok:
        ratio = len(rle_data) / len(expected) if len(expected) > 0 else 0
        print(f"RLE verified length    : {len(expected)} bytes")
        print("PASS: binary RLE stream matches original FASTA bases")
        print(f"Compression ratio: {ratio:.4f}")
        return

    print("FAIL: binary RLE stream could not be matched to original FASTA")
    print(f"Compressed offset reached: {comp_pos}")
    print(f"Sequence offset reached  : {seq_pos}")

    if comp_pos < len(rle_data):
        print(f"Compressed byte there    : 0x{rle_data[comp_pos]:02X}")

    if seq_pos < len(expected):
        print(f"Expected base there      : {chr(expected[seq_pos])}")

    sys.exit(1)


if __name__ == "__main__":
    main()