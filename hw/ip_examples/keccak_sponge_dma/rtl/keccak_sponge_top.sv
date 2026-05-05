import keccak_sponge_reg_pkg::*;

module keccak_sponge_top #(
    parameter type reg_req_t = logic,
    parameter type reg_rsp_t = logic
) (
    input logic clk_i,
    input logic rst_ni,

    input  reg_req_t reg_req_i,
    output reg_rsp_t reg_rsp_o,

    output logic        obi_req_o,
    input  logic        obi_gnt_i,
    output logic        obi_we_o,
    output logic [31:0] obi_addr_o,
    output logic [31:0] obi_wdata_o,
    output logic [ 3:0] obi_be_o,
    input  logic        obi_rvalid_i,
    input  logic [31:0] obi_rdata_i,

    output logic intr_o
);

    keccak_sponge_reg2hw_t reg2hw;
    keccak_sponge_hw2reg_t hw2reg;
    logic start_pulse;
    logic ctrl_q_d;
    logic core_busy;
    logic core_done;
    logic core_error;
    logic core_intr;

    keccak_sponge_reg_top #(
        .reg_req_t(reg_req_t),
        .reg_rsp_t(reg_rsp_t)
    ) i_reg_top (
        .clk_i    (clk_i),
        .rst_ni   (rst_ni),
        .reg_req_i(reg_req_i),
        .reg_rsp_o(reg_rsp_o),
        .reg2hw   (reg2hw),
        .hw2reg   (hw2reg),
        .devmode_i(1'b1)
    );

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            ctrl_q_d <= 1'b0;
        end else begin
            ctrl_q_d <= reg2hw.ctrl.q;
        end
    end

    assign start_pulse = reg2hw.ctrl.q & ~ctrl_q_d;

    assign hw2reg.status.busy.d = core_busy;
    assign hw2reg.status.busy.de = 1'b1;
    assign hw2reg.status.done.d = core_done;
    assign hw2reg.status.done.de = 1'b1;
    assign hw2reg.status.error.d = core_error;
    assign hw2reg.status.error.de = 1'b1;

    assign intr_o = core_intr;

    keccak_sponge_dma i_keccak_sponge_dma (
        .clk       (clk_i),
        .rst_n     (rst_ni),
        .start_i   (start_pulse),
        .src_addr_i(reg2hw.src_addr.q),
        .dst_addr_i(reg2hw.dst_addr.q),
        .data_len_i(reg2hw.data_len.q),
        .out_len_i (reg2hw.out_len.q),
        .domain_i  (reg2hw.domain.q),
        .mode_i    (2'd1),

        .busy_o       (core_busy),
        .done_o       (core_done),
        .error_o      (core_error),
        .keccak_intr_o(core_intr),

        .obi_req_o   (obi_req_o),
        .obi_gnt_i   (obi_gnt_i),
        .obi_we_o    (obi_we_o),
        .obi_addr_o  (obi_addr_o),
        .obi_wdata_o (obi_wdata_o),
        .obi_be_o    (obi_be_o),
        .obi_rvalid_i(obi_rvalid_i),
        .obi_rdata_i (obi_rdata_i)
    );

endmodule
