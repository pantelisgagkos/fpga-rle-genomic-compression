`timescale 1ns / 1ps

module rle_fasta_axis_core (
    input  wire        aclk,
    input  wire        aresetn,

    // AXI-Stream input from AXI DMA MM2S
    input  wire [7:0]  s_axis_tdata,
    input  wire        s_axis_tvalid,
    output wire        s_axis_tready,
    input  wire        s_axis_tlast,

    // AXI-Stream output to AXI DMA S2MM
    output reg  [7:0]  m_axis_tdata,
    output reg         m_axis_tvalid,
    input  wire        m_axis_tready,
    output reg         m_axis_tlast,

    // Debug / measurement counters
    output reg [31:0]  original_size,
    output reg [31:0]  run_count,
    output reg [31:0]  compressed_size,
    output reg         done
);

localparam [7:0] FILE_END_MARKER = 8'h00;
localparam [7:0] DUMMY_BYTE = 8'h00;

    // FSM states
    localparam S_READ          = 4'd0;
    localparam S_HEADER        = 4'd1;
    localparam S_EMIT_PREP     = 4'd2;
    localparam S_EMIT_COUNT    = 4'd3;
    localparam S_EMIT_CHAR     = 4'd4;
    localparam S_EMIT_LITERAL  = 4'd5;
    localparam S_POST_EMIT     = 4'd6;
    localparam S_SEND_DUMMY    = 4'd7;

    reg [3:0] state;

    reg [7:0]  current_char;
    reg [31:0] current_count;
    reg        started;

    reg [7:0]  pending_char;
    reg        pending_valid;

    reg [7:0]  emit_char;
    reg [31:0] emit_count;
    reg [31:0] emit_index;
    reg        final_emit;

    reg chunk_end_pending;
    reg resume_in_header;
    reg file_complete_latched;

    wire input_fire;
    wire file_end_fire;

    assign s_axis_tready = (state == S_READ) || (state == S_HEADER);
    assign input_fire    = s_axis_tvalid && s_axis_tready;
    assign file_end_fire = input_fire && s_axis_tlast && (s_axis_tdata == FILE_END_MARKER);

    // Convert lowercase ASCII letters to uppercase.
    function [7:0] to_upper;
        input [7:0] c;
        begin
            if (c >= 8'h61 && c <= 8'h7A)
                to_upper = c - 8'd32;
            else
                to_upper = c;
        end
    endfunction

    function is_newline;
        input [7:0] c;
        begin
            is_newline = (c == 8'h0A) || (c == 8'h0D);
        end
    endfunction

    always @(posedge aclk) begin
        if (!aresetn) begin
            state           <= S_READ;

            current_char    <= 8'd0;
            current_count   <= 32'd0;
            started         <= 1'b0;

            pending_char    <= 8'd0;
            pending_valid   <= 1'b0;

            emit_char       <= 8'd0;
            emit_count      <= 32'd0;
            emit_index      <= 32'd0;

            final_emit      <= 1'b0;
            chunk_end_pending <= 1'b0;
            resume_in_header <= 1'b0;
            file_complete_latched <= 1'b0;

            m_axis_tdata    <= 8'd0;
            m_axis_tvalid   <= 1'b0;
            m_axis_tlast    <= 1'b0;

            original_size   <= 32'd0;
            run_count       <= 32'd0;
            compressed_size <= 32'd0;
            done            <= 1'b0;
        end else begin
            case (state)

                S_READ: begin
                    m_axis_tvalid <= 1'b0;
                    m_axis_tlast  <= 1'b0;

                    if (input_fire) begin
                        done <= 1'b0;

        		        if (file_complete_latched) begin
            		        original_size   <= 32'd0;
            		        run_count       <= 32'd0;
           		            compressed_size <= 32'd0;
                            file_complete_latched <= 1'b0;
                        end

                        if (file_end_fire) begin
                            final_emit <= 1'b1;
                            chunk_end_pending <= 1'b0;
                            pending_valid <= 1'b0;
                            state <= S_EMIT_PREP;

                        end else if (s_axis_tdata == 8'h3E) begin // '>' FASTA header
                            if (s_axis_tlast) begin
                                resume_in_header <= 1'b1;
                                chunk_end_pending <= 1'b1;
                                state <= S_SEND_DUMMY;
                            end else begin
                                    state <= S_HEADER;
                            end
            
                        end else if (is_newline(s_axis_tdata)) begin
                            if (s_axis_tlast) begin
                                resume_in_header <= 1'b0;
                                chunk_end_pending <= 1'b1;
                                state <= S_SEND_DUMMY;
                            end
                        end else begin
                            // Valid sequence byte
                            if (file_complete_latched)
                                original_size <= 32'd1;
                            else
                                original_size <= original_size + 32'd1;

                            if (!started) begin
                                current_char  <= to_upper(s_axis_tdata);
                                current_count <= 32'd1;
                                started       <= 1'b1;

                                if (s_axis_tlast) begin
                                    resume_in_header <=1'b0;
                                    chunk_end_pending <= 1'b1;
                                    state      <= S_SEND_DUMMY;
                                end
                            end else if (to_upper(s_axis_tdata) == current_char) begin
                                current_count <= current_count + 32'd1;

                                if (s_axis_tlast) begin
                                    resume_in_header <= 1'b0;
                                    chunk_end_pending <= 1'b1;
                                    state      <= S_SEND_DUMMY;
                                end
                            end else begin
                                // New run begins, but first emit the previous run.
                                pending_char  <= to_upper(s_axis_tdata);
                                pending_valid <= 1'b1;
                                final_emit <= 1'b0;
                                resume_in_header <= 1'b0;
                                chunk_end_pending <= s_axis_tlast;
                                state         <= S_EMIT_PREP;
                            end
                        end
                    end
                end

                S_HEADER: begin
                    m_axis_tvalid <= 1'b0;
                    m_axis_tlast  <= 1'b0;

                    if (input_fire) begin
                        done <= 1'b0;
                        if (file_end_fire) begin
                            final_emit <= 1'b1;
                            chunk_end_pending <= 1'b0;
                            pending_valid <= 1'b0;
                            state      <= S_EMIT_PREP;
                            
                    end else if (s_axis_tdata == 8'h0A) begin
                        if (s_axis_tlast) begin
                            resume_in_header <= 1'b0;
                            chunk_end_pending <= 1'b1;
                            state <= S_SEND_DUMMY;
                        end else begin
                            state <= S_READ;
                        end
                    end else if (s_axis_tlast) begin
                        resume_in_header <= 1'b1;
                        chunk_end_pending <= 1'b1;
                        state <= S_SEND_DUMMY;
                    end
                    end
                end

                S_EMIT_PREP: begin
                    // Prepare current run for byte-stream output.
                    emit_char  <= current_char;
                    emit_count <= current_count;
                    emit_index <= 32'd0;

                    run_count <= run_count + 32'd1;

                    if (current_count > 32'd5)
                        compressed_size <= compressed_size + 32'd5;
                    else
                        compressed_size <= compressed_size + current_count;

                    m_axis_tvalid <= 1'b0;
                    m_axis_tlast  <= 1'b0;

                    if (current_count > 32'd5)
                        state <= S_EMIT_COUNT;
                    else
                        state <= S_EMIT_LITERAL;
                end

                S_EMIT_COUNT: begin
                    // Emit count as 32-bit little-endian binary value.
                    if (!m_axis_tvalid) begin
                        case (emit_index)
                            32'd0: m_axis_tdata <= emit_count[7:0];
                            32'd1: m_axis_tdata <= emit_count[15:8];
                            32'd2: m_axis_tdata <= emit_count[23:16];
                            32'd3: m_axis_tdata <= emit_count[31:24];
                            default: m_axis_tdata <= 8'd0;
                        endcase

                        m_axis_tvalid <= 1'b1;
                        m_axis_tlast  <= 1'b0;
                    end else if (m_axis_tready) begin
                        m_axis_tvalid <= 1'b0;
                        m_axis_tlast  <= 1'b0;

                        if (emit_index == 32'd3) begin
                            emit_index <= 32'd0;
                            state      <= S_EMIT_CHAR;
                        end else begin
                            emit_index <= emit_index + 32'd1;
                        end
                    end
                end
                
                S_EMIT_CHAR: begin
                    // Emit run character after the 32-bit count.
                    if (!m_axis_tvalid) begin
                    m_axis_tdata  <= emit_char;
                    m_axis_tvalid <= 1'b1;
                    m_axis_tlast  <= final_emit;
                    end else if (m_axis_tready) begin
                    m_axis_tvalid <= 1'b0;
                    m_axis_tlast  <= 1'b0;

                    if (final_emit) begin
                        // Final byte of packet was accepted. Re-arm immediately.
                        done          <= 1'b1;
                        started       <= 1'b0;

                        current_char  <= 8'd0;
                        current_count <= 32'd0;

                        pending_char  <= 8'd0;
                        pending_valid <= 1'b0;

                        emit_char     <= 8'd0;
                        emit_count    <= 32'd0;
                        emit_index    <= 32'd0;
                        final_emit    <= 1'b0;
                        chunk_end_pending <= 1'b0;
                        resume_in_header <= 1'b0;
                        file_complete_latched <= 1'b1;

                        state         <= S_READ;
                    end else begin
                        state <= S_POST_EMIT;
                    end
                    end
                end
                
                S_EMIT_LITERAL: begin
                    // Emit current character literally emit_count times.
                    if (!m_axis_tvalid) begin
                    m_axis_tdata  <= emit_char;
                    m_axis_tvalid <= 1'b1;
                    m_axis_tlast  <= final_emit && (emit_index == emit_count - 32'd1);
                    end else if (m_axis_tready) begin
                    m_axis_tvalid <= 1'b0;
                    m_axis_tlast  <= 1'b0;

                    if (emit_index == emit_count - 32'd1) begin
                        if (final_emit) begin
                        // Final byte of packet was accepted. Re-arm immediately.
                        done          <= 1'b1;
                        started       <= 1'b0;

                        current_char  <= 8'd0;
                        current_count <= 32'd0;

                        pending_char  <= 8'd0;
                        pending_valid <= 1'b0;

                        emit_char     <= 8'd0;
                        emit_count    <= 32'd0;
                        emit_index    <= 32'd0;
                        final_emit    <= 1'b0;

                        chunk_end_pending     <= 1'b0;
                        resume_in_header      <= 1'b0;
                        file_complete_latched <= 1'b1;

                        state         <= S_READ;
                        end else begin
                        state <= S_POST_EMIT;
                        end
                    end else begin
                        emit_index <= emit_index + 32'd1;
                    end
                    end
                end

                        

                        
                        
                S_POST_EMIT: begin
                    m_axis_tvalid <= 1'b0;
                    m_axis_tlast  <= 1'b0;

                    if (pending_valid) begin
                    // End of AXI-Stream packet. Re-arm immediately for next packet.
                    current_char  <= pending_char;
                    current_count <= 32'd1;
                    started       <= 1'b1;
                    pending_valid <= 1'b0;

                    if (chunk_end_pending)
                        state <= S_SEND_DUMMY;
                    else
                        state <= S_READ;
                    end else if (chunk_end_pending)begin
                        state <= S_SEND_DUMMY;
                    end else begin
                        state <= S_READ;
                    end
                end

                    

                S_SEND_DUMMY: begin
                    if (!m_axis_tvalid) begin
                        m_axis_tdata <= DUMMY_BYTE;
                        m_axis_tvalid <= 1'b1;
                        m_axis_tlast <= 1'b1;
                    end else if (m_axis_tready) begin
                            m_axis_tvalid <= 1'b0;
                            m_axis_tlast <= 1'b0;
                            done <= 1'b1;
                            chunk_end_pending <= 1'b0;

                            if (resume_in_header)
                                state <= S_HEADER;
                            else
                                state <= S_READ;
                    end
                end

                default: begin
                    state <= S_READ;
               end
            endcase
        end
    end
endmodule


                    

                    
