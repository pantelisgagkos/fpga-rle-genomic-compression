//State definition
`define S_READ 3'd0 
`define S_HEADER 3'd1
`define S_FIRST 3'd2
`define S_COMPARE 3'd3
`define S_SAME 3'd4
`define S_DIFF 3'd5
`define S_FLUSH 3'd6

module rle_fa_improved (out_char,out_count,out_valid,byte_ready,original_size,run_count,compressed_size,
byte_in,byte_valid,eof,clk,reset);

output reg [7:0] out_char;
output reg [31:0] out_count;
output reg out_valid;
output byte_ready;
output reg [31:0] original_size;
output reg [31:0] run_count;
output reg [31:0] compressed_size;

input [7:0] byte_in;
input byte_valid;
input clk;
input reset;
input eof;


reg [7:0] current_char;
reg [31:0] current_count;
reg started;
reg [2:0] state;
reg [2:0] next_state;
reg [7:0] saved_byte;

assign byte_ready = (state == `S_READ) || (state == `S_HEADER);

always @(posedge clk) begin
    if (reset) begin
        current_char <= 8'd0;
        current_count <= 32'd0;
        started <= 1'b0;

        out_char <= 8'd0;
        out_count <= 32'd0;
        out_valid <= 1'b0;

        saved_byte <= 8'd0;

        original_size <= 32'd0;
        run_count <= 32'd0;
        compressed_size <= 32'd0;

        state <= `S_READ;
    end
    else begin
        state <= next_state;
        out_valid <= 1'b0;

        if (state == `S_READ && byte_valid && byte_ready) begin
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

        case (state)
        `S_FIRST: begin
            started <= 1'b1;
            current_char <= saved_byte;
            current_count <= 32'd1;
        end

        `S_SAME: begin
            current_count <= current_count + 1;
        end

        `S_DIFF: begin
            out_char <= current_char;
            out_count <= current_count;
            out_valid <= 1'b1;
            run_count <= run_count +1;
            original_size <= original_size + current_count;
            if (current_count > 32'd1)
            compressed_size <= compressed_size + 32'd5;
            else
            compressed_size <= compressed_size + 32'd1;


            current_char <= saved_byte;
            current_count <= 32'd1;
        end

        `S_FLUSH: begin
            out_char <= current_char;
            out_count <= current_count;
            out_valid <= 1'b1;
            run_count <= run_count +1;
            original_size <= original_size + current_count;
            if (current_count > 32'd1)
            compressed_size <= compressed_size + 32'd5;
            else
            compressed_size <= compressed_size + 32'd1;
            
            started <= 1'b0;
            current_char <= 8'd0;
            current_count <= 32'd0;
        end
        endcase
    end
end
                        
always @(*) begin
    next_state = state;
        
    case(state)

    `S_READ: begin
        if (eof) begin
            if (started)
            next_state = `S_FLUSH;
            else
            next_state = `S_READ;
        end
        else begin
            if (byte_valid) begin
                if (byte_in == 8'h3E) //'>'
                next_state = `S_HEADER;  
                else if (byte_in == 8'h0A) //'\n'
                next_state = `S_READ;
                else begin
                if (started == 1'b0)
                next_state = `S_FIRST;
                else
                next_state = `S_COMPARE;
                end
            end
        end
    end
    `S_HEADER: begin
        if (byte_valid) begin
            if (byte_in == 8'h0A) //'\n'
            next_state = `S_READ;
        end
    end
    `S_FIRST: begin
        next_state = `S_READ; 
    end
    `S_COMPARE: begin
        if (saved_byte == current_char)
        next_state = `S_SAME;
        else
        next_state = `S_DIFF;
    end
    `S_SAME: begin
        next_state = `S_READ;
    end
    `S_DIFF: begin
        next_state = `S_READ;
        end
    `S_FLUSH: begin
        next_state = `S_READ;
    end
    default : begin
        next_state = `S_READ;
    end
    endcase
    end

endmodule