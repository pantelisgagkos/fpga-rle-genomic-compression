`timescale 1ns / 1ps

module rle_fastq_axis_core (
    input  wire        aclk,
    input  wire        aresetn,

    input  wire [7:0]  s_axis_tdata,
    input  wire        s_axis_tvalid,
    output wire        s_axis_tready,
    input  wire        s_axis_tlast,

    output reg  [7:0]  m_axis_tdata,
    output reg         m_axis_tvalid,
    input  wire        m_axis_tready,
    output reg         m_axis_tlast,

    output reg [31:0]  original_size,
    output reg [31:0]  run_count,
    output reg [31:0]  compressed_size,
    output reg         done
);

    localparam C_PARSE = 1'b0;
    localparam C_EMIT  = 1'b1;

    localparam P_HEADER  = 2'd0;
    localparam P_SEQ     = 2'd1;
    localparam P_PLUS    = 2'd2;
    localparam P_QUAL    = 2'd3;

    reg        core_state;
    reg [1:0]  parse_state;
    reg packet_active;

    reg [7:0]  current_char;
    reg [31:0] current_count;
    reg        started;

    reg [7:0]  emit_char;
    reg [31:0] emit_count;
    reg [2:0]  emit_len;
    reg [2:0]  emit_pos;
    reg        emit_tlast_after;
    reg [7:0] nb;

    wire input_fire;
    assign input_fire = s_axis_tvalid && s_axis_tready;

    assign s_axis_tready = (core_state == C_PARSE);

    function is_lf;
        input [7:0] c;
        begin
            is_lf = (c == 8'h0A);
        end
    endfunction

    function is_cr;
        input [7:0] c;
        begin
            is_cr = (c == 8'h0D);
        end
    endfunction

    function [7:0] normalize_seq;
        input [7:0] c;
        begin
            if (c == 8'h61)       normalize_seq = 8'h41; // a -> A
            else if (c == 8'h63)  normalize_seq = 8'h43; // c -> C
            else if (c == 8'h67)  normalize_seq = 8'h47; // g -> G
            else if (c == 8'h74)  normalize_seq = 8'h54; // t -> T
            else if (c == 8'h6E)  normalize_seq = 8'h4E; // n -> N
            else                  normalize_seq = c;
        end
    endfunction

    function is_seq_base;
        input [7:0] c;
        begin
            is_seq_base =
                (c == 8'h41) || // A
                (c == 8'h43) || // C
                (c == 8'h47) || // G
                (c == 8'h54) || // T
                (c == 8'h4E);   // N
        end
    endfunction

    task begin_emit;
        input [7:0]  ch;
        input [31:0] cnt;
        input        last_after;
        begin
            emit_char        <= ch;
            emit_count       <= cnt;
            emit_pos         <= 3'd0;
            emit_tlast_after <= last_after;

            if (cnt > 32'd5)
                emit_len <= 3'd5;
            else
                emit_len <= cnt[2:0];

            core_state <= C_EMIT;

            run_count <= run_count + 32'd1;
            original_size <= original_size + cnt;

            if (cnt > 32'd5)
                compressed_size <= compressed_size + 32'd5;
            else
                compressed_size <= compressed_size + cnt;
        end
    endtask

    task process_rle_byte;
        input [7:0] b;
        input       last_after;
        begin
            if (!started) begin
                current_char  <= b;
                current_count <= 32'd1;
                started       <= 1'b1;

                if (last_after) begin
                    begin_emit(b, 32'd1, 1'b1);
                    started       <= 1'b0;
                    current_char  <= 8'd0;
                    current_count <= 32'd0;
                end
            end
            else if (b == current_char) begin
                current_count <= current_count + 32'd1;

                if (last_after) begin
                    begin_emit(current_char, current_count + 32'd1, 1'b1);
                    started       <= 1'b0;
                    current_char  <= 8'd0;
                    current_count <= 32'd0;
                end
            end
            else begin
                begin_emit(current_char, current_count, last_after);

                current_char  <= b;
                current_count <= 32'd1;
                started       <= 1'b1;
            end
        end
    endtask

    always @(*) begin
        m_axis_tvalid = (core_state == C_EMIT);
        m_axis_tlast  = (core_state == C_EMIT) &&
                        emit_tlast_after &&
                        (emit_pos == (emit_len - 3'd1));

        m_axis_tdata = 8'd0;

        if (core_state == C_EMIT) begin
            if (emit_count > 32'd5) begin
                case (emit_pos)
                    3'd0: m_axis_tdata = emit_count[7:0];
                    3'd1: m_axis_tdata = emit_count[15:8];
                    3'd2: m_axis_tdata = emit_count[23:16];
                    3'd3: m_axis_tdata = emit_count[31:24];
                    3'd4: m_axis_tdata = emit_char;
                    default: m_axis_tdata = 8'd0;
                endcase
            end
            else begin
                m_axis_tdata = emit_char;
            end
        end
    end

    always @(posedge aclk) begin
        if (!aresetn) begin
            core_state <= C_PARSE;
            parse_state <= P_HEADER;
            packet_active <= 1'b0;

            current_char <= 8'd0;
            current_count <= 32'd0;
            started <= 1'b0;

            emit_char <= 8'd0;
            emit_count <= 32'd0;
            emit_len <= 3'd0;
            emit_pos <= 3'd0;
            emit_tlast_after <= 1'b0;

            original_size <= 32'd0;
            run_count <= 32'd0;
            compressed_size <= 32'd0;
            done <= 1'b0;
        end
        else begin

    /*
     * Start of a new AXI-Stream packet/chunk.
     * Since the ARM now sends record-aligned FASTQ chunks, each chunk
     * starts at a FASTQ header line.
     */
    if ((core_state == C_PARSE) && input_fire && !packet_active) begin
        packet_active   <= 1'b1;

        original_size   <= 32'd0;
        run_count       <= 32'd0;
        compressed_size <= 32'd0;
        done            <= 1'b0;

        parse_state     <= P_HEADER;
        started         <= 1'b0;
        current_char    <= 8'd0;
        current_count   <= 32'd0;
        
         emit_char        <= 8'd0;
         emit_count       <= 32'd0;
         emit_len         <= 3'd0;
         emit_pos         <= 3'd0;
         emit_tlast_after <= 1'b0;
    end
    else if ((core_state == C_PARSE) && input_fire) begin
        done <= 1'b0;
    end

    if (core_state == C_EMIT) begin
        if (m_axis_tready) begin
            if (emit_pos == (emit_len - 3'd1)) begin
                core_state <= C_PARSE;
                emit_pos <= 3'd0;

                if (emit_tlast_after) begin
                    done <= 1'b1;
                    packet_active <= 1'b0;
                    
                    parse_state <= P_HEADER;
                    started <= 1'b0;
                    current_char <= 8'd0;
                    current_count <= 32'd0;
                    
                     emit_char <= 8'd0;
                     emit_count <= 32'd0;
                     emit_len <= 3'd0;
                     emit_tlast_after <= 1'b0;
                end
            end
            else begin
                emit_pos <= emit_pos + 3'd1;
            end
        end
    end
    else begin
        if (input_fire) begin
            case (parse_state)

                P_HEADER: begin
                    if (is_lf(s_axis_tdata)) begin
                        parse_state <= P_SEQ;
                    end
                end

                P_SEQ: begin
                    if (is_cr(s_axis_tdata)) begin
                        // ignore CR
                    end
                    else if (is_lf(s_axis_tdata)) begin
                        parse_state <= P_PLUS;

                        if (started) begin
                            begin_emit(current_char, current_count, s_axis_tlast);
                            started <= 1'b0;
                            current_char <= 8'd0;
                            current_count <= 32'd0;
                        end
                        else if (s_axis_tlast) begin
                            done <= 1'b1;
                            packet_active <= 1'b0;
                        end
                    end
                    else begin
                        nb = normalize_seq(s_axis_tdata);

                        if (is_seq_base(nb)) begin
                            process_rle_byte(nb, s_axis_tlast);
                        end
                        else if (s_axis_tlast) begin
                            if (started) begin
                                begin_emit(current_char, current_count, 1'b1);
                                started <= 1'b0;
                                current_char <= 8'd0;
                                current_count <= 32'd0;
                            end
                            else begin
                                done <= 1'b1;
                                packet_active <= 1'b0;
                            end
                        end
                    end
                end

                P_PLUS: begin
                    if (is_lf(s_axis_tdata)) begin
                        parse_state <= P_QUAL;
                    end
                end

                P_QUAL: begin
                    if (is_cr(s_axis_tdata)) begin
                        // ignore CR
                    end
                    else if (is_lf(s_axis_tdata)) begin
                        parse_state <= P_HEADER;

                        if (started) begin
                            begin_emit(current_char, current_count, s_axis_tlast);
                            started <= 1'b0;
                            current_char <= 8'd0;
                            current_count <= 32'd0;
                        end
                        else if (s_axis_tlast) begin
                            done <= 1'b1;
                            packet_active <= 1'b0;
                        end
                    end
                    else begin
                        process_rle_byte(s_axis_tdata, s_axis_tlast);
                    end
                end

                default: begin
                    parse_state <= P_HEADER;
                end

            endcase
        end
    end
end
end

endmodule