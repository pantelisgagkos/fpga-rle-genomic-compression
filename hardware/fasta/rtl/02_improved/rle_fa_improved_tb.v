`timescale 1ns/1ps

module rle_fa_improved_tb;

reg clk;
reg reset;

reg [7:0] byte_in;
reg byte_valid;
reg eof;

wire [7:0] out_char;
wire [31:0] out_count;
wire out_valid;
wire byte_ready;
wire [31:0] original_size;
wire [31:0] run_count;
wire [31:0] compressed_size;

integer fd;
integer c;
real compression_ratio;

rle_fa_improved dut(
    .out_char(out_char),
    .out_count(out_count),
    .out_valid(out_valid),
    .byte_ready(byte_ready),
    .original_size(original_size),
    .run_count(run_count),
    .compressed_size(compressed_size),
    .byte_in(byte_in),
    .byte_valid(byte_valid),
    .eof(eof),
    .clk(clk),
    .reset(reset)
);

task send_byte;

    input [7:0] b;
    begin
        @(negedge clk);
        while (byte_ready == 1'b0)
        @(negedge clk);

        byte_in = b;
        byte_valid = 1'b1;

        @(negedge clk);
        byte_valid = 1'b0;
        byte_in = 8'd0;
    end
endtask 

initial begin
    clk = 1'b0;
    forever #5 clk = ~clk;

end

initial begin
    reset = 1'b1;
    byte_in = 8'd0;
    byte_valid = 1'b0;
    eof = 1'b0;

    #20;
    reset = 1'b0;


    fd = $fopen("/home/pantelis/Documents/test2.fa", "r");

    if (fd == 0)begin
        $display("ERROR: Could not open file.");
        $finish;
    end

    while (!$feof(fd)) begin
        c = $fgetc(fd);

        if (c != -1) begin
            send_byte(c[7:0]);

        end
    end

    $fclose(fd);

    @(negedge clk);
    while (byte_ready == 1'b0)
    @(negedge clk);

    eof = 1'b1;
    @(negedge clk);
    eof = 1'b0;

    repeat (5) @(posedge clk);
    if (original_size != 0) begin
        compression_ratio = compressed_size * 1.0 / original_size;
        $display("Compression ratio  = %0.4f", compression_ratio);
    end
    $display("Original size   = %0d bytes", original_size);
    $display("Run count       = %0d", run_count);
    $display("Compressed size = %0d bytes", compressed_size);
    $finish;
end

always @(posedge clk) begin
    if (out_valid) begin
        if (out_count > 32'd1) 
        $display("RLE: %0d%c", out_count, out_char); 
        else
        $display("RLE: %0b%c", 1,out_char);
    end
end



endmodule