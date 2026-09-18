module rle_fq_top (
    input clk,
    input reset,
    input [7:0] byte_in,
    input byte_valid,
    input eof,

    output byte_ready,
    output [7:0] out_char,
    output out_valid,
    output out_quality
);

wire [31:0] out_count;
wire [63:0] original_size;
wire [63:0] compressed_size;
wire [63:0] run_count;

rle_fq_optimized dut (
    .clk(clk),
    .reset(reset),
    .byte_in(byte_in),
    .byte_valid(byte_valid),
    .eof(eof),

    .byte_ready(byte_ready),

    .out_char(out_char),
    .out_count(out_count),
    .out_valid(out_valid),
    .out_quality(out_quality),

    .original_size(original_size),
    .compressed_size(compressed_size),
    .run_count(run_count)
);

endmodule
