`timescale 1ns / 1ps

//State definition
`define S_READ 4'd0
`define S_HEADER 4'd1
`define S_SEQUENCE 4'd2
`define S_FIRST 4'd3
`define S_COMPARE 4'd4
`define S_SAME 4'd5
`define S_DIFF 4'd6
`define S_PLUS 4'd7
`define S_QUALITY 4'd8
`define S_FLUSH 4'd9

module rle_fq (out_char,out_count,out_valid,byte_ready,out_quality,original_size,run_count,compressed_size,byte_in,byte_valid,eof,clk,reset);

output reg [7:0] out_char;
output reg [7:0] out_count;
output reg out_valid;
output byte_ready;
output reg out_quality;
output reg [63:0] original_size;
output reg [63:0] run_count;
output reg [63:0] compressed_size;

input [7:0] byte_in;
input byte_valid;
input clk;
input reset;
input eof;

reg [7:0] current_char;
reg [31:0] current_count;
reg started;
reg [3:0] state;
reg [3:0] next_state;
reg [7:0] saved_byte;
reg [3:0] return_state;


assign byte_ready = (state == `S_READ) || (state == `S_HEADER) || (state == `S_SEQUENCE) || (state == `S_PLUS) || (state == `S_QUALITY);

always @(posedge clk) begin
    if (reset) begin
        current_char <= 8'd0;
        current_count <= 32'd0;
        started <= 1'b0;

        out_char <= 8'd0;
        out_count <= 8'd0;
        out_valid <= 1'b0;

        saved_byte <= 8'd0;

        original_size <= 32'd0;
        run_count <= 32'd0;
        compressed_size <= 32'd0;

        return_state <= `S_READ;
        out_quality <= 1'b0;

        state <= `S_READ;
    end
    else begin
        state <= next_state;
        out_valid <= 1'b0;

        if (state == `S_SEQUENCE && byte_valid && byte_ready) begin
            if (byte_in == 8'h61) //'a'
            saved_byte <= 8'h41;
            else if (byte_in == 8'h63) // 'c'
            saved_byte <= 8'h43;
            else if (byte_in == 8'h67) //'g'
            saved_byte <= 8'h47;
            else if (byte_in == 8'h74 ) // 't'
            saved_byte <= 8'h54; 
            else
            saved_byte <= byte_in;
        end

        if (state == `S_QUALITY && byte_valid && byte_ready) begin
            saved_byte <= byte_in;
        end

        case (state)
        
        `S_SEQUENCE: begin
            return_state <= `S_SEQUENCE;
        end

        `S_QUALITY: begin
            return_state <= `S_QUALITY;
        end

        `S_FIRST: begin
            started <= 1'b1;
            current_char <= saved_byte;
            current_count <= 32'd1;
        end

        `S_SAME: begin
            current_count <= current_count + 1;
        end

        `S_DIFF: begin
            out_quality <= (return_state == `S_QUALITY);
            out_char <= current_char;
            out_count <= current_count;
            out_valid <= 1'b1;
            run_count <= run_count+1;
            original_size <= original_size + current_count;
            compressed_size <= compressed_size + 32'd2;

            current_char <= saved_byte;
            current_count <= 32'd1;
        end

        `S_FLUSH: begin
            out_quality <= (return_state == `S_QUALITY);
            out_char <= current_char;
            out_count <= current_count;
            out_valid <= 1'b1;
            run_count <= run_count +1;
            original_size <= original_size + current_count;
            compressed_size <= compressed_size + 32'd2;
            
            started <= 1'b0;
            current_char <= 8'd0;
            current_count <= 32'd0;
        end
        endcase
    end
end

always @(*) begin
    next_state = state;

    case (state)
    

    `S_READ:begin
        if (eof) begin
            if (started)
            next_state = `S_FLUSH;
            else
            next_state = `S_READ;
        end
        else begin
            if (byte_valid) begin
                if (byte_in == 8'h40) // '@'
                next_state = `S_HEADER;
                else 
                next_state = `S_READ;
            end
        end
    end

    `S_HEADER: begin
        if (byte_valid) begin
            if (byte_in == 8'h0A) //'\n'
            next_state = `S_SEQUENCE;
        end
    end

    `S_SEQUENCE:begin
        if (byte_valid)begin
            if (byte_in == 8'h0A) // '\n'
            next_state = `S_FLUSH;
            else if (started == 1'b0)
            next_state = `S_FIRST;
            else
            next_state = `S_COMPARE;
        end
        
    end

    `S_QUALITY:begin
        if (byte_valid)begin
            if (byte_in == 8'h0A) // '\n'
            next_state = `S_FLUSH;
            else if (started == 1'b0)
            next_state = `S_FIRST;
            else
            next_state = `S_COMPARE;
        end

    end

    `S_FIRST: begin
        if (return_state == `S_SEQUENCE)
        next_state = `S_SEQUENCE;
        else
        next_state = `S_QUALITY;
    end
    
    `S_COMPARE: begin
        if (saved_byte == current_char)
        next_state = `S_SAME;
        else
        next_state = `S_DIFF;
    end

    `S_SAME: begin
        if (return_state == `S_SEQUENCE)
        next_state = `S_SEQUENCE;
        else
        next_state = `S_QUALITY;
    end

    `S_DIFF: begin
        if (return_state == `S_SEQUENCE)
        next_state = `S_SEQUENCE;
        else
        next_state = `S_QUALITY;
    end

    `S_PLUS: begin
        if (byte_valid) begin
            if (byte_in == 8'h0A) //'\n'
            next_state = `S_QUALITY;
        end
    end

    `S_FLUSH: begin
        if (return_state == `S_SEQUENCE)
        next_state = `S_PLUS;
        else if (return_state == `S_QUALITY)
        next_state = `S_READ;
        else
        next_state = `S_READ;
     end
     
     default: begin
        next_state = `S_READ;
     end

    endcase
 end


endmodule