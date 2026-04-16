module keccak (
    input logic clk,
    input logic rst_n,

    // DMA control-plane inputs
    input logic        start_i,
    input logic [31:0] src_addr_i,
    input logic [31:0] dst_addr_i,
    input logic [31:0] data_len_i,

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

    import common::*;

    localparam int unsigned DMA_WORD_BYTES = 4;
    localparam int unsigned KECCAK_BLOCK_WORDS = 50;
    localparam int unsigned KECCAK_BLOCK_BYTES = KECCAK_BLOCK_WORDS * DMA_WORD_BYTES;
    localparam int unsigned CNT_W = $clog2(KECCAK_BLOCK_WORDS + 1);
    localparam int unsigned OBI_TIMEOUT_CYCLES = 1024;
    localparam int unsigned CORE_TIMEOUT_CYCLES = 1024;
    localparam int unsigned OBI_TMO_W = $clog2(OBI_TIMEOUT_CYCLES + 1);
    localparam int unsigned CORE_TMO_W = $clog2(CORE_TIMEOUT_CYCLES + 1);
    localparam logic [CNT_W-1:0] BLOCK_WORDS_CNT = CNT_W'(KECCAK_BLOCK_WORDS);
    localparam logic [CNT_W-1:0] LAST_WORD_CNT = CNT_W'(KECCAK_BLOCK_WORDS - 1);
    localparam logic [OBI_TMO_W-1:0] OBI_TMO_LAST = OBI_TMO_W'(OBI_TIMEOUT_CYCLES - 1);
    localparam logic [CORE_TMO_W-1:0] CORE_TMO_LAST = CORE_TMO_W'(CORE_TIMEOUT_CYCLES - 1);

    typedef enum logic [2:0] {
        ST_IDLE,
        ST_READ_XFER,
        ST_CORE_START,
        ST_CORE_WAIT_BUSY,
        ST_CORE_WAIT_DONE,
        ST_WRITE_XFER,
        ST_ERROR,
        ST_DONE
    } dma_state_e;

    dma_state_e state_q, state_d;

    logic [31:0] src_addr_q, dst_addr_q;
    logic [31:0] din_words[0:KECCAK_BLOCK_WORDS-1];
    logic [31:0] dout_words[0:KECCAK_BLOCK_WORDS-1];
    logic [CNT_W-1:0] gnt_cnt_q;
    logic [CNT_W-1:0] rvalid_cnt_q;
    logic [CNT_W-1:0] outstanding_cnt_q;
    logic [OBI_TMO_W-1:0] obi_timeout_cnt_q;
    logic [CORE_TMO_W-1:0] core_timeout_cnt_q;
    logic [$clog2(KECCAK_BLOCK_WORDS)-1:0] req_word_idx;
    logic [$clog2(KECCAK_BLOCK_WORDS)-1:0] rsp_word_idx;

    logic [1599:0] core_din;
    logic [1599:0] core_dout;
    logic core_start;
    logic core_ready;

    logic latch_cfg;
    logic read_word_en;
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

    genvar w;
    generate
        for (w = 0; w < KECCAK_BLOCK_WORDS; w++) begin : g_word_map
            assign core_din[w*32+:32] = din_words[w];
            assign dout_words[w]      = core_dout[w*32+:32];
        end
    endgenerate

    assign req_word_idx = (gnt_cnt_q < BLOCK_WORDS_CNT) ? gnt_cnt_q[$clog2(KECCAK_BLOCK_WORDS)-1:0] : 
                            LAST_WORD_CNT[$clog2(KECCAK_BLOCK_WORDS)-1:0];
    assign rsp_word_idx = rvalid_cnt_q[$clog2(KECCAK_BLOCK_WORDS)-1:0];

    keccak_data inst_keccak_data (
        .clk  (clk),
        .rst_n(rst_n),
        .start(core_start),
        .Din  (core_din),
        .ready(core_ready),
        .Dout (core_dout)
    );

    always_comb begin
        state_d         = state_q;
        latch_cfg       = 1'b0;
        read_word_en    = 1'b0;
        cnt_gnt_inc     = 1'b0;
        cnt_rvalid_inc  = 1'b0;
        outstanding_inc = 1'b0;
        outstanding_dec = 1'b0;
        cnt_clr         = 1'b0;
        obi_tmo_clr     = 1'b1;
        obi_tmo_inc     = 1'b0;
        core_tmo_clr    = 1'b1;
        core_tmo_inc    = 1'b0;
        error_set       = 1'b0;
        core_start      = 1'b0;

        obi_req_o       = 1'b0;
        obi_we_o        = 1'b0;
        obi_addr_o      = 32'h0;
        obi_wdata_o     = 32'h0;
        obi_be_o        = 4'hF;

        case (state_q)
            ST_IDLE: begin
                if (start_i) begin
                    if (data_len_i == KECCAK_BLOCK_BYTES) begin
                        latch_cfg = 1'b1;
                        cnt_clr   = 1'b1;
                        state_d   = ST_READ_XFER;
                    end else begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_READ_XFER: begin
                obi_tmo_clr = 1'b0;
                obi_we_o = 1'b0;
                obi_addr_o = src_addr_q + {24'h0, req_word_idx, 2'b00};

                if (gnt_cnt_q < BLOCK_WORDS_CNT) begin
                    obi_req_o = 1'b1;
                end

                if (obi_req_o && obi_gnt_i) begin
                    cnt_gnt_inc = 1'b1;
                    outstanding_inc = 1'b1;
                    obi_tmo_clr = 1'b1;
                end

                if ((rvalid_cnt_q < BLOCK_WORDS_CNT) && obi_rvalid_i) begin
                    if (outstanding_cnt_q == '0) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end else begin
                        read_word_en = 1'b1;
                        cnt_rvalid_inc = 1'b1;
                        outstanding_dec = 1'b1;
                        obi_tmo_clr = 1'b1;

                        if (rvalid_cnt_q == LAST_WORD_CNT) begin
                            cnt_clr = 1'b1;
                            state_d = ST_CORE_START;
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

            ST_CORE_START: begin
                core_start = 1'b1;
                state_d    = ST_CORE_WAIT_BUSY;
            end

            ST_CORE_WAIT_BUSY: begin
                core_tmo_clr = 1'b0;
                if (!core_ready) begin
                    core_tmo_clr = 1'b1;
                    state_d = ST_CORE_WAIT_DONE;
                end else begin
                    core_tmo_inc = 1'b1;
                    if (core_timeout_cnt_q == CORE_TMO_LAST) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_CORE_WAIT_DONE: begin
                core_tmo_clr = 1'b0;
                if (core_ready) begin
                    core_tmo_clr = 1'b1;
                    cnt_clr = 1'b1;
                    state_d = ST_WRITE_XFER;
                end else begin
                    core_tmo_inc = 1'b1;
                    if (core_timeout_cnt_q == CORE_TMO_LAST) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end
                end
            end

            ST_WRITE_XFER: begin
                obi_tmo_clr = 1'b0;
                obi_we_o    = 1'b1;
                obi_addr_o  = dst_addr_q + {24'h0, req_word_idx, 2'b00};
                obi_wdata_o = dout_words[req_word_idx];

                if (gnt_cnt_q < BLOCK_WORDS_CNT) begin
                    obi_req_o = 1'b1;
                end

                if (obi_req_o && obi_gnt_i) begin
                    cnt_gnt_inc = 1'b1;
                    outstanding_inc = 1'b1;
                    obi_tmo_clr = 1'b1;
                end

                if ((rvalid_cnt_q < BLOCK_WORDS_CNT) && obi_rvalid_i) begin
                    if (outstanding_cnt_q == '0) begin
                        error_set = 1'b1;
                        state_d   = ST_ERROR;
                    end else begin
                        cnt_rvalid_inc = 1'b1;
                        outstanding_dec = 1'b1;
                        obi_tmo_clr = 1'b1;

                        if (rvalid_cnt_q == LAST_WORD_CNT) begin
                            state_d = ST_DONE;
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
        gnt_cnt_q          <= '0;
        rvalid_cnt_q       <= '0;
        outstanding_cnt_q  <= '0;
        obi_timeout_cnt_q  <= '0;
        core_timeout_cnt_q <= '0;
        done_q             <= 1'b0;
        error_q            <= 1'b0;
        intr_q             <= 1'b0;

        for (int wi = 0; wi < KECCAK_BLOCK_WORDS; wi++) begin
            din_words[wi] <= 32'h0;
        end
        end else begin
            state_q <= state_d;

            if (latch_cfg) begin
                src_addr_q <= src_addr_i;
                dst_addr_q <= dst_addr_i;
                done_q     <= 1'b0;
                error_q    <= 1'b0;
                intr_q     <= 1'b0;
            end else if (state_q == ST_DONE) begin
                done_q <= 1'b1;
                intr_q <= 1'b1;
            end

            if (error_set) begin
                error_q <= 1'b1;
                intr_q  <= 1'b1;
            end

            if (cnt_clr) begin
                gnt_cnt_q    <= '0;
                rvalid_cnt_q <= '0;
                outstanding_cnt_q <= '0;
            end else begin
                    if (cnt_gnt_inc) begin
                        gnt_cnt_q <= gnt_cnt_q + 1'b1;
                    end
                    if (cnt_rvalid_inc) begin
                        rvalid_cnt_q <= rvalid_cnt_q + 1'b1;
                    end

                unique case ({outstanding_inc, outstanding_dec})
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

            if (read_word_en) begin
                din_words[rsp_word_idx] <= obi_rdata_i;
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
