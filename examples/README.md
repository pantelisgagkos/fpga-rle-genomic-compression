# Small Example Inputs

This directory contains small FASTA and FASTQ inputs for quickly inspecting the selective RLE behavior without using the complete benchmark datasets.

The examples intentionally contain:

- short runs with `count <= 5`, which remain literal
- long runs with `count > 5`, which are encoded using the selective RLE representation

## FASTA

File:

    sample.fa

Input:

    >sample_1
    AAAAAACCCGGTTTTTT
    >sample_2
    NNNNNNACGT

After FASTA preprocessing, headers and line formatting are ignored and the sequence data is treated as a continuous logical stream.

Expected behavior includes:

    AAAAAA -> encoded, count = 6
    CCC    -> literal
    GG     -> literal
    TTTTTT -> encoded, count = 6
    NNNNNN -> encoded, count = 6
    ACGT   -> literal

## FASTQ

File:

    sample.fastq

The file contains two valid four-line FASTQ records.

Expected sequence behavior includes:

    AAAAAA -> encoded, count = 6
    CCC    -> literal
    TTTTT  -> literal
    GGGGGG -> encoded, count = 6

Expected quality behavior includes:

    IIIIII -> encoded, count = 6
    !!!    -> literal
    #####  -> literal
    HHHHHH -> encoded, count = 6

Sequence and quality are treated as logically independent RLE regions.

## Notes

An encoded run uses a 32-bit little-endian count followed by the repeated character.

These files are intended for small functional tests and demonstrations only. They are not benchmark datasets.

The complete `hg19.fa` and `ERR173280_1.fastq` datasets used for the final thesis evaluation are intentionally not included in this repository because of their large size.
