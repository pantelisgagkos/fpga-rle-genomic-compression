# Final Benchmark Results

This file summarizes the final results reported for the complete FASTA and FASTQ datasets.

## Compression Results

| Dataset | RLE payload | Compressed size | Compression ratio | Payload reduction |
|---|---:|---:|---:|---:|
| FASTA — `hg19.fa` | 3,137,161,264 sequence bytes | 2,876,685,416 bytes | R_sequence = 0.9170 | 8.30% |
| FASTQ — `ERR173280_1.fastq` | 3,099,458,802 sequence + quality bytes | 2,666,892,017 bytes | R_seq+qual = 0.8604 | 13.96% |

The payload-based ratios are used because they refer to the data actually processed by the RLE mechanism.

## Verification

### FASTA

- Stateful Software reference
- Block-by-block output-length comparison
- Byte-by-byte comparison of FPGA output against Software reference
- Final result: `debug_fail_count = 0`
- FNV-1a used as an additional consistency check

### FASTQ

For the complete `ERR173280_1.fastq` dataset:

- Software compressed size: `2,666,892,017 bytes`
- FPGA compressed size: `2,666,892,017 bytes`
- Software FNV-1a: `0x00F0A426`
- FPGA FNV-1a: `0x00F0A426`

For the full FASTQ dataset, verification was based on compressed size and FNV-1a agreement rather than a full internal byte-by-byte comparison.

FNV-1a is used as a practical consistency check and is not a mathematical proof of byte-stream identity.

## Processing Performance

| Dataset | Software processing time | FPGA processing time | Software throughput | FPGA throughput |
|---|---:|---:|---:|---:|
| `hg19.fa` | 31.919 ± 3.146 s | 260.526 s | 100.25 MB/s | 12.28 MB/s |
| `ERR173280_1.fastq` | 35.257 ± 7.680 s | 152.839 s | 140.33 MB/s | 32.37 MB/s |

Software values are mean ± sample standard deviation over five runs.

Each final FPGA result corresponds to one complete benchmark execution.

The two processing-time measurements do not represent identical internal processing paths:

- Software: preprocessing + RLE on data already available in memory.
- FPGA: DDR → AXI DMA MM2S → RLE accelerator → AXI DMA S2MM → DDR, including DMA programming and polling wait time.
- Ethernet transfer time is excluded from the FPGA processing-time measurement.

## Energy Assessment

### Software — Intel RAPL

CPU-package energy from separate energy-measurement runs:

| Dataset | CPU-package energy |
|---|---:|
| `hg19.fa` | 617.71 ± 109.39 J |
| `ERR173280_1.fastq` | 644.25 ± 142.35 J |

### FPGA — Vivado Report Power

Vivado estimated Total On-Chip Power:

`1.726 W`

Estimated on-chip energy:

| Dataset | Estimated FPGA on-chip energy |
|---|---:|
| `hg19.fa` | 449.67 J |
| `ERR173280_1.fastq` | 263.80 J |

The Vivado results are vectorless modeled estimates of on-chip power, not direct electrical measurements of the complete ZC702 board.

Intel RAPL and Vivado Report Power therefore refer to different measurement domains and must not be interpreted as equivalent whole-system energy measurements.

## Main Result

The final FPGA implementations were functionally verified against their corresponding Software references, but the evaluated FPGA architecture did not achieve a processing-time speedup over the Software implementation.
