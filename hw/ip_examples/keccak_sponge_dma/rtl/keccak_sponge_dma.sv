import common::*;

module keccak_sponge_dma (
    input logic clk,
    input logic rst_n,

    // DMA control-plane inputs
    input logic        start_i,
    input logic [31:0] src_addr_i,
    input logic [31:0] dst_addr_i,
    input logic [31:0] data_len_i,
    input logic [31:0] out_len_i,
    input logic [ 7:0] domain_i,
    input logic [ 1:0] mode_i,

    // DMA status
    output logic busy_o,
    output logic done_o,
    output logic error_o,
    output logic keccak_intr_o,

    // Minimal OBI-like master interface
    output logic        obi_req_o,
    input  logic        obi_gnt_i,
    output logic        obi_we_o,
    output logic [31:0] obi_addr_o,
    output logic [31:0] obi_wdata_o,
    output logic [ 3:0] obi_be_o,
    input  logic        obi_rvalid_i,
    input  logic [31:0] obi_rdata_i
);

    localparam int unsigned DMA_WORD_BYTES = 4;
    localparam int unsigned KECCAK_BLOCK_WORDS = 50;
    localparam int unsigned KECCAK_BLOCK_BYTES = KECCAK_BLOCK_WORDS * DMA_WORD_BYTES;
    localparam int unsigned KECCAK_MAX_XFER_BYTES = 256;
    localparam int unsigned SHAKE256_RATE_BYTES = 136;
    localparam int unsigned SHAKE256_RATE_WORDS = SHAKE256_RATE_BYTES / DMA_WORD_BYTES;
    localparam int unsigned CNT_W = $clog2(KECCAK_BLOCK_WORDS + 1);
    localparam int unsigned OBI_TIMEOUT_CYCLES = 1024;
    localparam int unsigned CORE_TIMEOUT_CYCLES = 1024;
    localparam int unsigned OBI_TMO_W = $clog2(OBI_TIMEOUT_CYCLES + 1);
    localparam int unsigned CORE_TMO_W = $clog2(CORE_TIMEOUT_CYCLES + 1);
    localparam logic [OBI_TMO_W-1:0] OBI_TMO_LAST = OBI_TMO_W'(OBI_TIMEOUT_CYCLES - 1);
    localparam logic [CORE_TMO_W-1:0] CORE_TMO_LAST = CORE_TMO_W'(CORE_TIMEOUT_CYCLES - 1);
    localparam logic [1:0] MODE_RAW_PERMUTE = 2'd0;
    localparam logic [1:0] MODE_SHAKE256_SPONGE = 2'd1;

    function automatic logic [CNT_W-1:0] bytes_to_words(input logic [31:0] byte_len);
        logic [CNT_W-1:0] whole_words;
        begin
            whole_words = CNT_W'(byte_len[31:2]);
            bytes_to_words = whole_words + ((byte_len[1:0] != 2'b00) ? CNT_W'(1) : CNT_W'(0));
        end
    endfunction

    function automatic logic [31:0] tail_word_mask(input logic [1:0] tail_bytes);
        begin
            unique case (tail_bytes)
                2'd0: tail_word_mask = 32'hFFFF_FFFF;
                2'd1: tail_word_mask = 32'h0000_00FF;
                2'd2: tail_word_mask = 32'h0000_FFFF;
                2'd3: tail_word_mask = 32'h00FF_FFFF;
                default: tail_word_mask = 32'hFFFF_FFFF;
            endcase
        end
    endfunction

    function automatic logic [3:0] tail_write_be(input logic [1:0] tail_bytes);
        begin
            unique case (tail_bytes)
                2'd0: tail_write_be = 4'b1111;
                2'd1: tail_write_be = 4'b0001;
                2'd2: tail_write_be = 4'b0011;
                2'd3: tail_write_be = 4'b0111;
                default: tail_write_be = 4'b1111;
            endcase
        end
    endfunction

    function automatic logic [31:0] clamp_raw_bytes(input logic [31:0] byte_len);
        begin
            if (byte_len > KECCAK_BLOCK_BYTES) begin
                clamp_raw_bytes = KECCAK_BLOCK_BYTES;
            end else begin
                clamp_raw_bytes = byte_len;
            end
        end
    endfunction

    function automatic logic [31:0] clamp_rate_bytes(input logic [31:0] byte_len);
        begin
            if (byte_len > SHAKE256_RATE_BYTES) begin
                clamp_rate_bytes = SHAKE256_RATE_BYTES;
            end else begin
                clamp_rate_bytes = byte_len;
            end
        end
    endfunction

    function automatic logic [31:0] pad_rate_word(
        input int unsigned word_idx, input logic [31:0] word, input logic [31:0] domain_offset,
        input logic [7:0] domain);
        logic [31:0] padded_word;
        begin
            padded_word = word;

            if (word_idx == int'(domain_offset[31:2])) begin
                unique case (domain_offset[1:0])
                    2'd0: padded_word[7:0] = padded_word[7:0] ^ domain;
                    2'd1: padded_word[15:8] = padded_word[15:8] ^ domain;
                    2'd2: padded_word[23:16] = padded_word[23:16] ^ domain;
                    2'd3: padded_word[31:24] = padded_word[31:24] ^ domain;
                    default: ;
                endcase
            end

            if (word_idx == (SHAKE256_RATE_WORDS - 1)) begin
                padded_word[31:24] = padded_word[31:24] ^ 8'h80;
            end

            pad_rate_word = padded_word;
        end
    endfunction

    typedef enum logic [4:0] {
        ST_IDLE,
        ST_RAW_READ,
        ST_RAW_CORE_START,
        ST_RAW_CORE_WAIT_BUSY,
        ST_RAW_CORE_WAIT_DONE,
        ST_RAW_WRITE,
        ST_SPONGE_ABSORB_PREP,
        ST_SPONGE_READ,
        ST_SPONGE_PAD,
        ST_SPONGE_ABSORB_CORE_START,
        ST_SPONGE_ABSORB_CORE_WAIT_BUSY,
        ST_SPONGE_ABSORB_CORE_WAIT_DONE,
        ST_SPONGE_SQUEEZE_PREP,
        ST_SPONGE_WRITE,
        ST_SPONGE_SQUEEZE_CORE_START,
        ST_SPONGE_SQUEEZE_CORE_WAIT_BUSY,
        ST_SPONGE_SQUEEZE_CORE_WAIT_DONE,
        ST_ERROR,
        ST_DONE
    } dma_state_e;

    dma_state_e state_q, state_d;

    logic [31:0] src_addr_q, dst_addr_q;
    logic [31:0] byte_offset_q;
    logic [31:0] out_offset_q;
    logic [31:0] bytes_remaining_q;
    logic [31:0] out_remaining_q;
    logic [31:0] chunk_bytes_q;
    logic [31:0] out_len_q;
    logic [7:0] domain_q;
    logic [1:0] mode_q;
    logic [31:0] din_words[0:KECCAK_BLOCK_WORDS-1];
    logic [CNT_W-1:0] word_count_q;
    logic [CNT_W-1:0] last_word_cnt_q;
    logic [1:0] tail_bytes_q;
    logic [31:0] tail_mask_q;
    logic [3:0] last_word_be_q;
    logic [CNT_W-1:0] gnt_cnt_q;
    logic [CNT_W-1:0] rvalid_cnt_q;
    logic [CNT_W-1:0] outstanding_cnt_q;
    logic [OBI_TMO_W-1:0] obi_timeout_cnt_q;
    logic [CORE_TMO_W-1:0] core_timeout_cnt_q;
    logic [31:0] first_raw_chunk_bytes_i;
    logic [31:0] next_raw_remaining_bytes_i;
    logic [31:0] next_raw_chunk_bytes_i;
    logic [31:0] next_sponge_remaining_bytes_i;
    logic [31:0] next_squeeze_remaining_bytes_i;
    logic [31:0] next_squeeze_chunk_bytes_i;
    logic [31:0] absorb_chunk_bytes_i;
    logic [31:0] squeeze_chunk_bytes_i;
    logic [CNT_W-1:0] first_raw_chunk_words_i;
    logic [CNT_W-1:0] next_raw_chunk_words_i;
    logic [CNT_W-1:0] absorb_chunk_words_i;
    logic [CNT_W-1:0] squeeze_chunk_words_i;
    logic [CNT_W-1:0] req_word_idx;
    logic [CNT_W-1:0] rsp_word_idx;

    logic [1599:0] core_din;
    logic [1599:0] core_dout;
    logic core_start;
    logic core_ready;

    logic latch_raw_cfg;
    logic latch_sponge_cfg;
    logic setup_absorb_chunk;
    logic setup_empty_pad;
    logic setup_squeeze_chunk;
    logic read_raw_word_en;
    logic read_sponge_word_en;
    logic apply_sponge_pad;
    logic latch_core_output;
    logic advance_raw_chunk;
    logic advance_absorb_chunk;
    logic advance_squeeze_chunk;
    logic cnt_gnt_inc;
    logic cnt_rvalid_inc;
    logic outstanding_inc;
    logic outstanding_dec;
    logic cnt_clr;
    logic obi_tmo_clr;
    logic obi_tmo_inc;
    logic core_tmo_clr;
    logic core_tmo_inc;
    logic done_q;
    logic error_q;
    logic intr_q;
    logic error_set;
    logic final_block_q;

    genvar w;
    generate
        for (w = 0; w < KECCAK_BLOCK_WORDS; w++) begin : g_word_map
            assign core_din[w*32+:32] = din_words[w];
        end
    endgenerate

    assign first_raw_chunk_bytes_i = clamp_raw_bytes(data_len_i);
    assign first_raw_chunk_words_i = bytes_to_words(first_raw_chunk_bytes_i);
    assign next_raw_remaining_bytes_i = bytes_remaining_q - chunk_bytes_q;
    assign next_raw_chunk_bytes_i = clamp_raw_bytes(next_raw_remaining_bytes_i);
    assign next_raw_chunk_words_i = bytes_to_words(next_raw_chunk_bytes_i);
    assign next_sponge_remaining_bytes_i = bytes_remaining_q - chunk_bytes_q;
    assign next_squeeze_remaining_bytes_i = out_remaining_q - chunk_bytes_q;
    assign next_squeeze_chunk_bytes_i = clamp_rate_bytes(next_squeeze_remaining_bytes_i);
    assign absorb_chunk_bytes_i = clamp_rate_bytes(bytes_remaining_q);
    assign squeeze_chunk_bytes_i = clamp_rate_bytes(out_remaining_q);
    assign absorb_chunk_words_i = bytes_to_words(absorb_chunk_bytes_i);
    assign squeeze_chunk_words_i = bytes_to_words(squeeze_chunk_bytes_i);

    assign req_word_idx = (gnt_cnt_q < word_count_q) ? gnt_cnt_q : last_word_cnt_q;
    assign rsp_word_idx = rvalid_cnt_q;

    keccak_data inst_keccak_data (
        .clk  (clk),
        .rst_n(rst_n),
        .start(core_start),
        .Din  (core_din),
        .ready(core_ready),
        .Dout (core_dout)
    );

    always_comb begin
        state_d               = state_q;
        latch_raw_cfg         = 1'b0;
        latch_sponge_cfg      = 1'b0;
        setup_absorb_chunk    = 1'b0;
        setup_empty_pad       = 1'b0;
        setup_squeeze_chunk   = 1'b0;
        read_raw_word_en      = 1'b0;
        read_sponge_word_en   = 1'b0;
        apply_sponge_pad      = 1'b0;
        latch_core_output     = 1'b0;
        advance_raw_chunk     = 1'b0;
        advance_absorb_chunk  = 1'b0;
        advance_squeeze_chunk = 1'b0;
        cnt_gnt_inc           = 1'b0;
        cnt_rvalid_inc        = 1'b0;
        outstanding_inc       = 1'b0;
        outstanding_dec       = 1'b0;
        cnt_clr               = 1'b0;
        obi_tmo_clr           = 1'b1;
        obi_tmo_inc           = 1'b0;
        core_tmo_clr          = 1'b1;
        core_tmo_inc          = 1'b0;
        error_set             = 1'b0;
        core_start            = 1'b0;

        obi_req_o             = 1'b0;
        obi_we_o              = 1'b0;
        obi_addr_o            = 32'h0;
        obi_wdata_o           = 32'h0;
        obi_be_o              = 4'hF;

        case (state_q)
            ST_IDLE: begin
                if (start_i) begin
                    unique case (mode_i)
                        MODE_RAW_PERMUTE: begin
                            if ((data_len_i != 32'h0) && (data_len_i <= KECCAK_MAX_XFER_BYTES)) begin
                                latch_raw_cfg = 1'b1;
                                cnt_clr = 1'b1;
                                state_d = ST_RAW_READ;
                            end else begin
                                error_set = 1'b1;
                                state_d   = ST_ERROR;
                            end
                        end

                        MODE_SHAKE256_SPONGE: begin
                            if (out_len_i != 32'h0) begin
                                latch_sponge_cfg = 1'b1;
                                cnt_clr = 1'b1;
                                state_d = ST_SPONGE_ABSORB_PREP;
                            end else begin
                                error_set = 1'b1;
                                state_d   = ST_ERROR;
                            end
                        end

                        default: begin
                            error_set = 1'b1;
                            state_d   = ST_ERROR;
                        end
                    endcase
                end
            end

            ST_RAW_READ: begin
                obi_tmo_clr = 1'b0;
                obi_we_o = 1'b0;
                obi_addr_o = src_addr_q + byte_offset_q + {24'h0, req_word_idx[5:0], 2'b00};

                if (gnt_cnt_q < word_count_q) begin
                    obi_req_o = 1'b1;
                end

                if (obi_req_o && obi_gnt_i) begin
                    cnt_gnt_inc = 1'b1;
                    outstanding_inc = 1'b1;
                    obi_tmo_clr = 1'b1;
                end

                if ((rvalid_cnt_q < word_count_q) && obi_rvalid_i) begin
                    if (outstanding_cnt_q == '0) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end else begin
                        read_raw_word_en = 1'b1;
                        cnt_rvalid_inc = 1'b1;
                        outstanding_dec = 1'b1;
                        obi_tmo_clr = 1'b1;

                        if (rvalid_cnt_q == last_word_cnt_q) begin
                            cnt_clr = 1'b1;
                            state_d = ST_RAW_CORE_START;
                        end
                    end
                end

                if (!obi_tmo_clr) begin
                    obi_tmo_inc = 1'b1;
                    if (obi_timeout_cnt_q == OBI_TMO_LAST) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_RAW_CORE_START: begin
                core_start = 1'b1;
                state_d = ST_RAW_CORE_WAIT_BUSY;
            end

            ST_RAW_CORE_WAIT_BUSY: begin
                core_tmo_clr = 1'b0;
                if (!core_ready) begin
                    core_tmo_clr = 1'b1;
                    state_d = ST_RAW_CORE_WAIT_DONE;
                end else begin
                    core_tmo_inc = 1'b1;
                    if (core_timeout_cnt_q == CORE_TMO_LAST) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_RAW_CORE_WAIT_DONE: begin
                core_tmo_clr = 1'b0;
                if (core_ready) begin
                    core_tmo_clr = 1'b1;
                    latch_core_output = 1'b1;
                    cnt_clr = 1'b1;
                    state_d = ST_RAW_WRITE;
                end else begin
                    core_tmo_inc = 1'b1;
                    if (core_timeout_cnt_q == CORE_TMO_LAST) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_RAW_WRITE: begin
                obi_tmo_clr = 1'b0;
                obi_we_o    = 1'b1;
                obi_addr_o  = dst_addr_q + byte_offset_q + {24'h0, req_word_idx[5:0], 2'b00};
                obi_wdata_o = din_words[req_word_idx];
                obi_be_o    = (gnt_cnt_q == last_word_cnt_q) ? last_word_be_q : 4'hF;

                if (gnt_cnt_q < word_count_q) begin
                    obi_req_o = 1'b1;
                end

                if (obi_req_o && obi_gnt_i) begin
                    cnt_gnt_inc = 1'b1;
                    outstanding_inc = 1'b1;
                    obi_tmo_clr = 1'b1;
                end

                if ((rvalid_cnt_q < word_count_q) && obi_rvalid_i) begin
                    if (outstanding_cnt_q == '0) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end else begin
                        cnt_rvalid_inc = 1'b1;
                        outstanding_dec = 1'b1;
                        obi_tmo_clr = 1'b1;

                        if (rvalid_cnt_q == last_word_cnt_q) begin
                            if (bytes_remaining_q > chunk_bytes_q) begin
                                cnt_clr = 1'b1;
                                advance_raw_chunk = 1'b1;
                                state_d = ST_RAW_READ;
                            end else begin
                                state_d = ST_DONE;
                            end
                        end
                    end
                end

                if (!obi_tmo_clr) begin
                    obi_tmo_inc = 1'b1;
                    if (obi_timeout_cnt_q == OBI_TMO_LAST) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_SPONGE_ABSORB_PREP: begin
                if (bytes_remaining_q != 32'h0) begin
                    setup_absorb_chunk = 1'b1;
                    cnt_clr = 1'b1;
                    state_d = ST_SPONGE_READ;
                end else begin
                    setup_empty_pad = 1'b1;
                    state_d = ST_SPONGE_PAD;
                end
            end

            ST_SPONGE_READ: begin
                obi_tmo_clr = 1'b0;
                obi_we_o = 1'b0;
                obi_addr_o = src_addr_q + byte_offset_q + {24'h0, req_word_idx[5:0], 2'b00};

                if (gnt_cnt_q < word_count_q) begin
                    obi_req_o = 1'b1;
                end

                if (obi_req_o && obi_gnt_i) begin
                    cnt_gnt_inc = 1'b1;
                    outstanding_inc = 1'b1;
                    obi_tmo_clr = 1'b1;
                end

                if ((rvalid_cnt_q < word_count_q) && obi_rvalid_i) begin
                    if (outstanding_cnt_q == '0) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end else begin
                        read_sponge_word_en = 1'b1;
                        cnt_rvalid_inc = 1'b1;
                        outstanding_dec = 1'b1;
                        obi_tmo_clr = 1'b1;

                        if (rvalid_cnt_q == last_word_cnt_q) begin
                            cnt_clr = 1'b1;
                            if (chunk_bytes_q < SHAKE256_RATE_BYTES) begin
                                state_d = ST_SPONGE_PAD;
                            end else begin
                                state_d = ST_SPONGE_ABSORB_CORE_START;
                            end
                        end
                    end
                end

                if (!obi_tmo_clr) begin
                    obi_tmo_inc = 1'b1;
                    if (obi_timeout_cnt_q == OBI_TMO_LAST) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_SPONGE_PAD: begin
                apply_sponge_pad = 1'b1;
                state_d = ST_SPONGE_ABSORB_CORE_START;
            end

            ST_SPONGE_ABSORB_CORE_START: begin
                core_start = 1'b1;
                state_d = ST_SPONGE_ABSORB_CORE_WAIT_BUSY;
            end

            ST_SPONGE_ABSORB_CORE_WAIT_BUSY: begin
                core_tmo_clr = 1'b0;
                if (!core_ready) begin
                    core_tmo_clr = 1'b1;
                    state_d = ST_SPONGE_ABSORB_CORE_WAIT_DONE;
                end else begin
                    core_tmo_inc = 1'b1;
                    if (core_timeout_cnt_q == CORE_TMO_LAST) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_SPONGE_ABSORB_CORE_WAIT_DONE: begin
                core_tmo_clr = 1'b0;
                if (core_ready) begin
                    core_tmo_clr = 1'b1;
                    latch_core_output = 1'b1;
                    if (final_block_q) begin
                        state_d = ST_SPONGE_SQUEEZE_PREP;
                    end else begin
                        advance_absorb_chunk = 1'b1;
                        state_d = ST_SPONGE_ABSORB_PREP;
                    end
                end else begin
                    core_tmo_inc = 1'b1;
                    if (core_timeout_cnt_q == CORE_TMO_LAST) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_SPONGE_SQUEEZE_PREP: begin
                setup_squeeze_chunk = 1'b1;
                cnt_clr = 1'b1;
                state_d = ST_SPONGE_WRITE;
            end

            ST_SPONGE_WRITE: begin
                obi_tmo_clr = 1'b0;
                obi_we_o    = 1'b1;
                obi_addr_o  = dst_addr_q + out_offset_q + {24'h0, req_word_idx[5:0], 2'b00};
                obi_wdata_o = din_words[req_word_idx];
                obi_be_o    = (gnt_cnt_q == last_word_cnt_q) ? last_word_be_q : 4'hF;

                if (gnt_cnt_q < word_count_q) begin
                    obi_req_o = 1'b1;
                end

                if (obi_req_o && obi_gnt_i) begin
                    cnt_gnt_inc = 1'b1;
                    outstanding_inc = 1'b1;
                    obi_tmo_clr = 1'b1;
                end

                if ((rvalid_cnt_q < word_count_q) && obi_rvalid_i) begin
                    if (outstanding_cnt_q == '0) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end else begin
                        cnt_rvalid_inc = 1'b1;
                        outstanding_dec = 1'b1;
                        obi_tmo_clr = 1'b1;

                        if (rvalid_cnt_q == last_word_cnt_q) begin
                            if (out_remaining_q > chunk_bytes_q) begin
                                cnt_clr = 1'b1;
                                advance_squeeze_chunk = 1'b1;
                                state_d = ST_SPONGE_SQUEEZE_CORE_START;
                            end else begin
                                state_d = ST_DONE;
                            end
                        end
                    end
                end

                if (!obi_tmo_clr) begin
                    obi_tmo_inc = 1'b1;
                    if (obi_timeout_cnt_q == OBI_TMO_LAST) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_SPONGE_SQUEEZE_CORE_START: begin
                core_start = 1'b1;
                state_d = ST_SPONGE_SQUEEZE_CORE_WAIT_BUSY;
            end

            ST_SPONGE_SQUEEZE_CORE_WAIT_BUSY: begin
                core_tmo_clr = 1'b0;
                if (!core_ready) begin
                    core_tmo_clr = 1'b1;
                    state_d = ST_SPONGE_SQUEEZE_CORE_WAIT_DONE;
                end else begin
                    core_tmo_inc = 1'b1;
                    if (core_timeout_cnt_q == CORE_TMO_LAST) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_SPONGE_SQUEEZE_CORE_WAIT_DONE: begin
                core_tmo_clr = 1'b0;
                if (core_ready) begin
                    core_tmo_clr = 1'b1;
                    latch_core_output = 1'b1;
                    state_d = ST_SPONGE_SQUEEZE_PREP;
                end else begin
                    core_tmo_inc = 1'b1;
                    if (core_timeout_cnt_q == CORE_TMO_LAST) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_ERROR: begin
                state_d = ST_IDLE;
            end

            ST_DONE: begin
                state_d = ST_IDLE;
            end

            default: begin
                state_d = ST_IDLE;
            end
        endcase
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state_q            <= ST_IDLE;
            src_addr_q         <= 32'h0;
            dst_addr_q         <= 32'h0;
            byte_offset_q      <= 32'h0;
            out_offset_q       <= 32'h0;
            bytes_remaining_q  <= 32'h0;
            out_remaining_q    <= 32'h0;
            chunk_bytes_q      <= 32'h0;
            out_len_q          <= 32'h0;
            domain_q           <= 8'h0;
            mode_q             <= MODE_RAW_PERMUTE;
            word_count_q       <= '0;
            last_word_cnt_q    <= '0;
            tail_bytes_q       <= 2'b00;
            tail_mask_q        <= 32'hFFFF_FFFF;
            last_word_be_q     <= 4'hF;
            gnt_cnt_q          <= '0;
            rvalid_cnt_q       <= '0;
            outstanding_cnt_q  <= '0;
            obi_timeout_cnt_q  <= '0;
            core_timeout_cnt_q <= '0;
            done_q             <= 1'b0;
            error_q            <= 1'b0;
            intr_q             <= 1'b0;
            final_block_q      <= 1'b0;

            for (int wi = 0; wi < KECCAK_BLOCK_WORDS; wi++) begin
                din_words[wi] <= 32'h0;
            end
        end else begin
            state_q <= state_d;

            if (latch_raw_cfg) begin
                src_addr_q <= src_addr_i;
                dst_addr_q <= dst_addr_i;
                byte_offset_q <= 32'h0;
                out_offset_q <= 32'h0;
                bytes_remaining_q <= data_len_i;
                out_remaining_q <= 32'h0;
                chunk_bytes_q <= first_raw_chunk_bytes_i;
                out_len_q <= 32'h0;
                domain_q <= 8'h0;
                mode_q <= MODE_RAW_PERMUTE;
                word_count_q <= first_raw_chunk_words_i;
                last_word_cnt_q <= first_raw_chunk_words_i - 1'b1;
                tail_bytes_q <= first_raw_chunk_bytes_i[1:0];
                tail_mask_q <= tail_word_mask(first_raw_chunk_bytes_i[1:0]);
                last_word_be_q <= tail_write_be(first_raw_chunk_bytes_i[1:0]);
                done_q <= 1'b0;
                error_q <= 1'b0;
                intr_q <= 1'b0;
                final_block_q <= 1'b0;

                for (int wi = 0; wi < KECCAK_BLOCK_WORDS; wi++) begin
                    din_words[wi] <= 32'h0;
                end
            end else if (latch_sponge_cfg) begin
                src_addr_q <= src_addr_i;
                dst_addr_q <= dst_addr_i;
                byte_offset_q <= 32'h0;
                out_offset_q <= 32'h0;
                bytes_remaining_q <= data_len_i;
                out_remaining_q <= out_len_i;
                chunk_bytes_q <= 32'h0;
                out_len_q <= out_len_i;
                domain_q <= domain_i;
                mode_q <= MODE_SHAKE256_SPONGE;
                word_count_q <= '0;
                last_word_cnt_q <= '0;
                tail_bytes_q <= 2'b00;
                tail_mask_q <= 32'hFFFF_FFFF;
                last_word_be_q <= 4'hF;
                done_q <= 1'b0;
                error_q <= 1'b0;
                intr_q <= 1'b0;
                final_block_q <= 1'b0;

                for (int wi = 0; wi < KECCAK_BLOCK_WORDS; wi++) begin
                    din_words[wi] <= 32'h0;
                end
            end else if (advance_raw_chunk) begin
                byte_offset_q <= byte_offset_q + chunk_bytes_q;
                bytes_remaining_q <= next_raw_remaining_bytes_i;
                chunk_bytes_q <= next_raw_chunk_bytes_i;
                word_count_q <= next_raw_chunk_words_i;
                last_word_cnt_q <= next_raw_chunk_words_i - 1'b1;
                tail_bytes_q <= next_raw_chunk_bytes_i[1:0];
                tail_mask_q <= tail_word_mask(next_raw_chunk_bytes_i[1:0]);
                last_word_be_q <= tail_write_be(next_raw_chunk_bytes_i[1:0]);

                for (int wi = 0; wi < KECCAK_BLOCK_WORDS; wi++) begin
                    din_words[wi] <= 32'h0;
                end
            end else if (setup_absorb_chunk) begin
                chunk_bytes_q <= absorb_chunk_bytes_i;
                word_count_q <= absorb_chunk_words_i;
                last_word_cnt_q <= absorb_chunk_words_i - 1'b1;
                tail_bytes_q <= absorb_chunk_bytes_i[1:0];
                tail_mask_q <= tail_word_mask(absorb_chunk_bytes_i[1:0]);
                last_word_be_q <= tail_write_be(absorb_chunk_bytes_i[1:0]);
            end else if (setup_empty_pad) begin
                chunk_bytes_q <= 32'h0;
                word_count_q <= '0;
                last_word_cnt_q <= '0;
                tail_bytes_q <= 2'b00;
                tail_mask_q <= 32'hFFFF_FFFF;
                last_word_be_q <= 4'hF;
            end else if (advance_absorb_chunk) begin
                byte_offset_q <= byte_offset_q + chunk_bytes_q;
                bytes_remaining_q <= next_sponge_remaining_bytes_i;
                final_block_q <= 1'b0;
            end else if (setup_squeeze_chunk) begin
                chunk_bytes_q <= squeeze_chunk_bytes_i;
                word_count_q <= squeeze_chunk_words_i;
                last_word_cnt_q <= squeeze_chunk_words_i - 1'b1;
                tail_bytes_q <= squeeze_chunk_bytes_i[1:0];
                tail_mask_q <= tail_word_mask(squeeze_chunk_bytes_i[1:0]);
                last_word_be_q <= tail_write_be(squeeze_chunk_bytes_i[1:0]);
            end else if (advance_squeeze_chunk) begin
                out_offset_q <= out_offset_q + chunk_bytes_q;
                out_remaining_q <= next_squeeze_remaining_bytes_i;
                chunk_bytes_q <= next_squeeze_chunk_bytes_i;
                word_count_q <= bytes_to_words(next_squeeze_chunk_bytes_i);
                last_word_cnt_q <= bytes_to_words(next_squeeze_chunk_bytes_i) - 1'b1;
                tail_bytes_q <= next_squeeze_chunk_bytes_i[1:0];
                tail_mask_q <= tail_word_mask(next_squeeze_chunk_bytes_i[1:0]);
                last_word_be_q <= tail_write_be(next_squeeze_chunk_bytes_i[1:0]);
            end else if (state_q == ST_DONE) begin
                done_q <= 1'b1;
                intr_q <= 1'b1;
            end

            if (error_set) begin
                error_q <= 1'b1;
                intr_q  <= 1'b1;
            end

            if (cnt_clr) begin
                gnt_cnt_q <= '0;
                rvalid_cnt_q <= '0;
                outstanding_cnt_q <= '0;
            end else begin
                if (cnt_gnt_inc) begin
                    gnt_cnt_q <= gnt_cnt_q + 1'b1;
                end
                if (cnt_rvalid_inc) begin
                    rvalid_cnt_q <= rvalid_cnt_q + 1'b1;
                end

                unique case ({
                    outstanding_inc, outstanding_dec
                })
                    2'b10:   outstanding_cnt_q <= outstanding_cnt_q + 1'b1;
                    2'b01:   outstanding_cnt_q <= outstanding_cnt_q - 1'b1;
                    default: ;
                endcase
            end

            if (obi_tmo_clr) begin
                obi_timeout_cnt_q <= '0;
            end else if (obi_tmo_inc) begin
                obi_timeout_cnt_q <= obi_timeout_cnt_q + 1'b1;
            end

            if (core_tmo_clr) begin
                core_timeout_cnt_q <= '0;
            end else if (core_tmo_inc) begin
                core_timeout_cnt_q <= core_timeout_cnt_q + 1'b1;
            end

            if (read_raw_word_en) begin
                if ((tail_bytes_q != 2'b00) && (rvalid_cnt_q == last_word_cnt_q)) begin
                    din_words[rsp_word_idx] <= obi_rdata_i & tail_mask_q;
                end else begin
                    din_words[rsp_word_idx] <= obi_rdata_i;
                end
            end

            if (read_sponge_word_en) begin
                if ((tail_bytes_q != 2'b00) && (rvalid_cnt_q == last_word_cnt_q)) begin
                    din_words[rsp_word_idx] <= din_words[rsp_word_idx] ^ (obi_rdata_i & tail_mask_q);
                end else begin
                    din_words[rsp_word_idx] <= din_words[rsp_word_idx] ^ obi_rdata_i;
                end
            end

            if (apply_sponge_pad) begin
                final_block_q <= 1'b1;
                for (int wi = 0; wi < SHAKE256_RATE_WORDS; wi++) begin
                    din_words[wi] <= pad_rate_word(wi, din_words[wi], chunk_bytes_q, domain_q);
                end
            end

            if (latch_core_output) begin
                for (int wi = 0; wi < KECCAK_BLOCK_WORDS; wi++) begin
                    din_words[wi] <= core_dout[wi*32+:32];
                end
            end
        end
    end

    assign busy_o        = (state_q != ST_IDLE) && (state_q != ST_DONE) && (state_q != ST_ERROR);
    assign done_o        = done_q;
    assign error_o       = error_q;
    assign keccak_intr_o = intr_q;

    // Trace support for verilator.
    initial begin
        if ($test$plusargs("trace") != 0) begin
            $display("[%0t] Tracing to logs/vlt_dump.vcd...\n", $time);
            $dumpfile("logs/vlt_dump.vcd");
            $dumpvars();
        end
        $display("[%0t] Model running...", $time);
    end
endmodule
