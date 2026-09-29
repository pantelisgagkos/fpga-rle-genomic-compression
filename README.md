# FPGA Acceleration of Genomic Data Compression with Run-Length Encoding

This repository contains the Software and FPGA implementations developed for the bachelor's thesis:

**Hardware Acceleration of Genomic Data Compression via Run-Length Encoding (RLE) on FPGA**

The project investigates selective Run-Length Encoding for genomic data stored in **FASTA** and **FASTQ** formats and evaluates corresponding Software and FPGA implementations on an AMD/Xilinx Zynq-7000 platform.

## Project Overview

The development followed the path:

**Software reference implementation → standalone RTL → AXI integration → AXI4-Stream + DMA → end-to-end Ethernet system → verification and benchmarking**

Two different final FPGA strategies were developed because FASTA and FASTQ have different structural requirements:

- **FASTA:** stateful processing across DMA chunks
- **FASTQ:** record-aligned DMA chunks

The Processing System (PS) performs communication, memory management and DMA control, while the Programmable Logic (PL) contains the custom RLE accelerator.

## Selective RLE Policy

A compressed run is represented by:

- a 32-bit run count
- one character byte

This requires 5 bytes per encoded run.

The final policy is therefore:

    count <= 5  -> emit the characters literally
    count > 5   -> emit 32-bit little-endian count + character

For example:

    AAAAAA

is represented as:

    [count = 6, 4 bytes] [A, 1 byte]

while:

    CCC

remains:

    CCC

The same selective policy is used by the final Software and FPGA implementations.

## FASTA Processing

FASTA input is preprocessed before RLE processing:

- header lines are ignored
- formatting characters are excluded
- sequence characters are processed as a continuous logical stream
- supported sequence characters are normalized where required

The final FASTA implementation is **stateful across DMA chunks**.

The active RLE context is preserved between consecutive transfers so that a run crossing a physical DMA boundary remains one logical run.

Conceptually:

    Chunk 1        Chunk 2
    AAAA           AAAA

              -> A x 8

Intermediate AXI4-Stream `TLAST` events represent DMA-transfer boundaries rather than logical end-of-file boundaries.

A dedicated end-of-file mechanism is used to flush the final active run only when the complete FASTA stream has ended.

## FASTQ Processing

FASTQ input follows the standard four-line record structure:

    Header
    Sequence
    Plus
    Quality

The final hardware parser follows the logical states:

    P_HEADER -> P_SEQ -> P_PLUS -> P_QUAL

Header and plus lines are used for structural parsing and are not forwarded to the RLE mechanism.

Sequence and quality are treated as logically independent RLE regions, while the same RLE logic is reused sequentially.

Unlike FASTA, the final FASTQ implementation uses **record-aligned DMA chunks**.

The Processing System collects data toward a target chunk size of:

    61,440 bytes

If that point is reached inside a FASTQ record, the chunk is extended until the record is complete.

Therefore:

    DMA chunk = integer number of complete FASTQ records

Each new FASTQ transfer can begin from `P_HEADER` with a clean RLE state.

## FPGA Platform

The hardware platform used in the project is:

- AMD/Xilinx ZC702 Evaluation Board
- Zynq-7000 XC7Z020-CLG484-1
- ARM Cortex-A9 Processing System
- Programmable Logic FPGA fabric
- DDR memory
- Ethernet interface

Main development tools:

- AMD Vivado 2025.2
- AMD Vitis 2025.2
- Verilog HDL
- C
- Python

The final PL and AXI4-Stream datapath operates at:

    50 MHz

## Final Hardware Architecture

The final processing path is:

    PC
     |
     | Ethernet / TCP
     v
    Processing System
    ARM Cortex-A9 + Vitis + lwIP
     |
     v
    DDR input buffer
     |
     v
    AXI DMA MM2S
     |
     v
    RLE AXI4-Stream accelerator
     |
     v
    AXI DMA S2MM
     |
     v
    DDR output buffer
     |
     v
    Processing System

During end-to-end functional tests, processed output can also be returned to the PC through TCP.

The architecture separates the high-volume streaming path from control operations:

- **AXI4-Stream:** accelerator input/output data
- **AXI DMA:** DDR-to-stream and stream-to-DDR transfers
- **GP0:** control/status path
- **HP0:** AXI DMA access to DDR

Each final bitstream contains one accelerator:

- stateful FASTA RLE accelerator, or
- record-aligned FASTQ RLE accelerator

## Architecture Evolution

### FASTA

The FASTA implementation evolved through:

    Basic standalone RLE
        ->
    Improved RLE
        ->
    Selective RLE
        ->
    AXI4-Lite integration
        ->
    AXI4-Stream + DMA
        ->
    Stateful AXI4-Stream + DMA

AXI4-Lite was used as the first FASTA PS-to-PL integration mechanism.

It verified communication with the custom accelerator, but the byte-oriented memory-mapped transactions required high PS involvement and were not retained as the main data path.

### FASTQ

The FASTQ implementation evolved through:

    Standalone FASTQ RLE
        ->
    Optimized selective RLE
        ->
    AXI4-Stream FASTQ accelerator

FASTQ moved from standalone RTL directly to the AXI4-Stream/DMA architecture.

There was no intermediate FASTQ AXI4-Lite implementation.

## Repository Structure

    fpga-rle-genomic-compression/
    |
    |-- hardware/
    |   |-- fasta/
    |   |   |-- rtl/
    |   |   `-- vivado/
    |   |
    |   `-- fastq/
    |       |-- rtl/
    |       `-- vivado/
    |
    |-- software/
    |   |-- reference/
    |   |   |-- fasta/
    |   |   `-- fastq/
    |   |
    |   |-- vitis/
    |   |   |-- fasta/
    |   |   `-- fastq/
    |   |
    |   `-- host/
    |
    |-- examples/
    |   |-- sample.fa
    |   |-- sample.fastq
    |   `-- README.md
    |
    |-- benchmarks/
    |   |-- scripts/
    |   `-- results/
    |       `-- final_results.md
    |
    `-- .gitignore

The repository preserves the main implementation stages instead of only the final versions, allowing the architectural evolution of the project to be followed.

## Software Reference Implementations

Separate Software reference implementations were developed for FASTA and FASTQ.

The Software versions evolved from initial validation implementations to streaming and chunk-based processing and were later used as references for FPGA verification and benchmarking.

The source files are located under:

    software/reference/fasta/
    software/reference/fastq/

## FPGA RTL Implementations

The main RTL development stages are located under:

    hardware/fasta/rtl/
    hardware/fastq/rtl/

The FASTA directory contains the progression from the initial standalone core through AXI4-Lite and AXI4-Stream integration to the final stateful accelerator.

The FASTQ directory contains the standalone and optimized implementations followed by the final AXI4-Stream accelerator.

## Vitis Applications

Processing System applications are stored under:

    software/vitis/fasta/
    software/vitis/fastq/

They manage:

- DDR buffers
- AXI DMA transfers
- communication with the Programmable Logic
- TCP communication through lwIP
- benchmark counters and verification information

The final DMA architecture operates in **Simple mode** and completion is monitored through polling.

## Host Communication

PC-side communication utilities are stored under:

    software/host/

The final host client communicates with the ZC702 using Ethernet/TCP.

The TCP server on the board uses port:

    7

The network configuration used during development was:

    ZC702 IP : 192.168.2.10
    Netmask  : 255.255.255.0
    Gateway  : 192.168.2.1

These settings can be changed for another environment.

## Vivado Project Recreation

Generated Vivado build directories are intentionally not version-controlled.

Portable Tcl recreation files are included instead.

For FASTA:

    hardware/fasta/vivado/system_bd.tcl
    hardware/fasta/vivado/recreate_project.tcl

The FASTA directory also contains the minimal packaged custom RLE IP required by the Block Design.

For FASTQ:

    hardware/fastq/vivado/system_bd.tcl
    hardware/fastq/vivado/recreate_project.tcl

The FASTQ Block Design uses the final FASTQ AXI4-Stream RTL core as a module reference.

With Vivado 2025.2 available, the designs can be recreated from the corresponding `recreate_project.tcl` scripts.

Generated `build/` directories are ignored by Git.

## Verification

### FASTA

The final stateful FASTA FPGA implementation was checked against the corresponding Software reference using:

- output-length comparison
- block-by-block comparison
- byte-by-byte comparison
- FNV-1a as an additional consistency check

The final full-dataset run reported:

    debug_fail_count = 0

No discrepancy was recorded in the examined FASTA results.

### FASTQ

For the complete FASTQ dataset, verification used:

- total compressed size
- FNV-1a hash

The final Software and FPGA values were:

    compressed size = 2,666,892,017 bytes
    FNV-1a          = 0x00F0A426

FNV-1a is used as a practical consistency check and is not considered a mathematical proof of byte-stream identity.

## Final Compression Results

| Dataset | RLE payload | Compressed size | Payload ratio | Payload reduction |
|---|---:|---:|---:|---:|
| `hg19.fa` | 3,137,161,264 B | 2,876,685,416 B | 0.9170 | 8.30% |
| `ERR173280_1.fastq` | 3,099,458,802 B | 2,666,892,017 B | 0.8604 | 13.96% |

For FASTA, the RLE payload corresponds to sequence data.

For FASTQ, the RLE payload corresponds to sequence + quality data.

These percentages refer to the specific evaluated datasets and are not intended as a general FASTA-versus-FASTQ comparison.

## Final Processing Performance

| Dataset | Software time | FPGA time | Software throughput | FPGA throughput |
|---|---:|---:|---:|---:|
| `hg19.fa` | 31.919 ± 3.146 s | 260.526 s | 100.25 MB/s | 12.28 MB/s |
| `ERR173280_1.fastq` | 35.257 ± 7.680 s | 152.839 s | 140.33 MB/s | 32.37 MB/s |

Software values are mean ± sample standard deviation over five runs.

Each final FPGA benchmark corresponds to one complete execution.

The measured processing paths are not internally identical.

Software measurement:

    preprocessing + RLE on data already available in memory

FPGA measurement:

    DDR
     -> DMA MM2S
     -> RLE accelerator
     -> DMA S2MM
     -> DDR
     + DMA programming and polling

Ethernet transfer time is excluded from the reported FPGA processing time.

In the evaluated configuration, the FPGA implementation did not achieve a processing-time speedup over the Software implementation.

## Energy Assessment

Software energy was measured using Intel RAPL at CPU-package level.

FPGA power was estimated using Vivado Report Power.

Vivado estimated Total On-Chip Power:

    1.726 W

| Dataset | Software CPU-package energy | FPGA estimated on-chip energy |
|---|---:|---:|
| `hg19.fa` | 617.71 ± 109.39 J | 449.67 J |
| `ERR173280_1.fastq` | 644.25 ± 142.35 J | 263.80 J |

These measurements refer to different domains.

Intel RAPL measures CPU-package energy, while Vivado Report Power provides a modeled on-chip estimate and does not measure the complete ZC702 board.

The values must therefore not be interpreted as equivalent whole-system energy measurements.

Detailed benchmark results are available in:

    benchmarks/results/final_results.md

## FPGA Resource Use

The final custom RLE cores use a relatively small amount of programmable logic.

| Resource | Stateful FASTA core | FASTQ core |
|---|---:|---:|
| LUTs | 273 | 449 |
| Flip-Flops | 236 | 189 |
| Block RAM Tiles | 0 | 0 |
| DSPs | 0 | 0 |

Both final designs satisfied the timing constraints at 50 MHz.

## Small Example Inputs

Small FASTA and FASTQ inputs are available under:

    examples/

They are intended for quick functional tests and demonstrations of the selective RLE policy and are not benchmark datasets.

## Datasets

The complete genomic datasets are not stored in the repository because of their large size.

The final benchmarks used:

    FASTA : hg19.fa
    FASTQ : ERR173280_1.fastq

The repository contains source code, project-recreation files, verification utilities and summarized benchmark results rather than the full genomic datasets.

## Current Limitations

Important limitations of the evaluated system include:

- 8-bit AXI4-Stream datapath
- temporary stalls while encoded or literal output bytes are emitted
- AXI DMA operating in Simple mode
- DMA completion handled through polling
- one complete final FPGA benchmark run per dataset
- FPGA power based on Vivado on-chip estimation rather than direct board-level measurement

The independent contribution of each architectural factor to total processing time was not isolated experimentally.

The current mixed literal/RLE representation is also not a standalone self-describing compressed file format because it does not include independent framing or tagging that allows literal and encoded tokens to be distinguished unambiguously from the compressed stream alone.

## Future Work

Possible extensions include:

- wider AXI4-Stream datapath
- deeper pipelining
- FIFO buffering
- greater hardware parallelism
- investigation of higher FPGA clock frequencies
- Scatter-Gather DMA
- double or ping-pong buffering
- interrupt-driven DMA completion
- more detailed performance profiling
- direct board-level power measurement
- self-describing compressed representation
- standalone decoder

## Main Conclusion

The project demonstrates the functional implementation of selective FASTA and FASTQ RLE processing on FPGA and verifies the final hardware results against the corresponding Software references.

The evaluated FPGA architecture did not achieve a processing-time speedup over the Software implementation.

However, the final accelerators occupy a relatively small amount of FPGA logic, leaving architectural headroom for wider, more parallel and more deeply pipelined future implementations.

---

Developed as a bachelor's thesis project at the **Department of Informatics and Telematics, Harokopio University of Athens**.

**Author:** Pantelis Gagkos
