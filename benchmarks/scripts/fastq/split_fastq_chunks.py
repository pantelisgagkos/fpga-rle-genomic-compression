#!/usr/bin/env python3

TARGET = 60 * 1024
inp = "test_fastq_200k.fastq"

buf = bytearray()
state = 0
chunk_id = 1

with open(inp, "rb") as f:
    while True:
        b = f.read(1)
        if not b:
            break

        buf.extend(b)

        if b == b"\n":
            state += 1
            if state >= 4:
                state = 0

            if len(buf) >= TARGET and state == 0:
                out = f"fastq_chunk_{chunk_id}.fastq"
                with open(out, "wb") as g:
                    g.write(buf)
                print(out, len(buf))
                chunk_id += 1
                buf.clear()

if buf:
    out = f"fastq_chunk_{chunk_id}.fastq"
    with open(out, "wb") as g:
        g.write(buf)
    print(out, len(buf))
