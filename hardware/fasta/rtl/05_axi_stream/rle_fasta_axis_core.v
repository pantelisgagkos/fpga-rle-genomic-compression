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

    // FSM states
    localparam S_READ          = 4'd0;
    localparam S_HEADER        = 4'd1;
    localparam S_EMIT_PREP     = 4'd2;
    localparam S_EMIT_COUNT    = 4'd3;
    localparam S_EMIT_CHAR     = 4'd4;
    localparam S_EMIT_LITERAL  = 4'd5;
    localparam S_POST_EMIT     = 4'd6;
    localparam S_DONE          = 4'd7;

    reg [3:0] state;

    reg [7:0]  current_char;
    reg [31:0] current_count;
    reg        started;

    reg [7:0]  pending_char;
    reg        pending_valid;
    reg        pending_last;

    reg [7:0]  emit_char;
    reg [31:0] emit_count;
    reg [31:0] emit_index;
    reg        final_emit;

    wire input_fire;

    assign s_axis_tready = (state == S_READ) || (state == S_HEADER);
    assign input_fire    = s_axis_tvalid && s_axis_tready;

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

    task finish_run;
        begin
            m_axis_tvalid <= 1'b0;
            m_axis_tlast  <= 1'b0;
            state         <= S_POST_EMIT;
        end
    endtask

    always @(posedge aclk) begin
        if (!aresetn) begin
            state           <= S_READ;

            current_char    <= 8'd0;
            current_count   <= 32'd0;
            started         <= 1'b0;

            pending_char    <= 8'd0;
            pending_valid   <= 1'b0;
            pending_last    <= 1'b0;

            emit_char       <= 8'd0;
            emit_count      <= 32'd0;
            emit_index      <= 32'd0;
            final_emit      <= 1'b0;

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
                 // If this is a new packet after a completed previous packet,
        	// clear per-packet counters/state before accepting the first byte.
        		if (done && !started) begin
            		original_size   <= 32'd0;
            		run_count       <= 32'd0;
           		compressed_size <= 32'd0;

            		current_char    <= 8'd0;
            		current_count   <= 32'd0;

            		pending_char    <= 8'd0;
            		pending_valid   <= 1'b0;
            		pending_last    <= 1'b0;

            		emit_char       <= 8'd0;
            		emit_count      <= 32'd0;
            		emit_index      <= 32'd0;
            		final_emit      <= 1'b0;
        		end
                    
                    
                        done <= 1'b0;

                        if (s_axis_tdata == 8'h3E) begin // '>' FASTA header
                            if (s_axis_tlast) begin
                                if (started) begin
                                    final_emit <= 1'b1;
                                    state      <= S_EMIT_PREP;
                                end else begin
                                    done  <= 1'b1;
                                    state <= S_DONE;
                                end
                            end else begin
                                state <= S_HEADER;
                            end
                        end else if (is_newline(s_axis_tdata)) begin
                            if (s_axis_tlast) begin
                                if (started) begin
                                    final_emit <= 1'b1;
                                    state      <= S_EMIT_PREP;
                                end else begin
                                    done  <= 1'b1;
                                    state <= S_DONE;
                                end
                            end
                        end else begin
                            // Valid sequence byte
                            original_size <= (done && !started) ? 32'd1 : (original_size + 32'd1);

                            if (!started) begin
                                current_char  <= to_upper(s_axis_tdata);
                                current_count <= 32'd1;
                                started       <= 1'b1;

                                if (s_axis_tlast) begin
                                    final_emit <= 1'b1;
                                    state      <= S_EMIT_PREP;
                                end
                            end else if (to_upper(s_axis_tdata) == current_char) begin
                                current_count <= current_count + 32'd1;

                                if (s_axis_tlast) begin
                                    final_emit <= 1'b1;
                                    state      <= S_EMIT_PREP;
                                end
                            end else begin
                                // New run begins, but first emit the previous run.
                                pending_char  <= to_upper(s_axis_tdata);
                                pending_valid <= 1'b1;
                                pending_last  <= s_axis_tlast;

                                final_emit    <= 1'b0;
                                state         <= S_EMIT_PREP;
                            end
                        end
                    end
                end

                S_HEADER: begin
                    m_axis_tvalid <= 1'b0;
                    m_axis_tlast  <= 1'b0;

                    if (input_fire) begin
                        if (s_axis_tlast) begin
                            if (started) begin
                                final_emit <= 1'b1;
                                state      <= S_EMIT_PREP;
                            end else begin
                                done  <= 1'b1;
                                state <= S_DONE;
                            end
                        end else if (s_axis_tdata == 8'h0A) begin
                            state <= S_READ;
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
			    pending_last  <= 1'b0;

			    emit_char     <= 8'd0;
			    emit_count    <= 32'd0;
			    emit_index    <= 32'd0;
			    final_emit    <= 1'b0;

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
				pending_last  <= 1'b0;

				emit_char     <= 8'd0;
				emit_count    <= 32'd0;
				emit_index    <= 32'd0;
				final_emit    <= 1'b0;

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

		    if (final_emit) begin
			// End of AXI-Stream packet. Re-arm immediately for next packet.
			done          <= 1'b1;
			started       <= 1'b0;

			current_char  <= 8'd0;
			current_count <= 32'd0;

			pending_char  <= 8'd0;
			pending_valid <= 1'b0;
			pending_last  <= 1'b0;

			emit_char     <= 8'd0;
			emit_count    <= 32'd0;
			emit_index    <= 32'd0;
			final_emit    <= 1'b0;

			state         <= S_READ;
		    end else if (pending_valid) begin
			current_char  <= pending_char;
			current_count <= 32'd1;
			started       <= 1'b1;

			final_emit    <= pending_last;
			pending_valid <= 1'b0;
			pending_last  <= 1'b0;

			if (pending_last)
			    state <= S_EMIT_PREP;
			else
			    state <= S_READ;
		    end else begin
			state <= S_READ;
		    end
		end

               

                S_DONE: begin
                    m_axis_tvalid <= 1'b0;
                    m_axis_tlast  <= 1'b0;
                    // Keep done high so software/debug can see that the previous
    		   // packet completed, but re-arm the core for the next AXI-Stream packet.
                    done          <= 1'b1;
                    started       <= 1'b0;
    		    pending_valid <= 1'b0;
    		    pending_last  <= 1'b0;
    		    final_emit    <= 1'b0;
    		    
    		    state         <= S_READ;
                    
                end

                default: begin
                    state <= S_READ;
                end

            endcase
        end
    end

endmodule
