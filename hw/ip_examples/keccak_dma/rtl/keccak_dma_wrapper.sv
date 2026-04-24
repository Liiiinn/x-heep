module keccak_dma_wrapper #(
    parameter type reg_req_t  = logic,
    parameter type reg_rsp_t  = logic,
    parameter type obi_req_t  = logic,
    parameter type obi_resp_t = logic
) (
    input logic clk_i,
    input logic rst_ni,

    input  reg_req_t reg_req_i,
    output reg_rsp_t reg_rsp_o,

    output obi_req_t  keccak_req_o,
    input  obi_resp_t keccak_resp_i,

    output logic keccak_intr_o
);

    logic        obi_req;
    logic        obi_we;
    logic [31:0] obi_addr;
    logic [31:0] obi_wdata;
    logic [ 3:0] obi_be;
    logic        obi_gnt;
    logic        obi_rvalid;
    logic [31:0] obi_rdata;

    assign keccak_req_o.req = obi_req;
    assign keccak_req_o.we = obi_we;
    assign keccak_req_o.addr = obi_addr;
    assign keccak_req_o.wdata = obi_wdata;
    assign keccak_req_o.be = obi_be;

    assign obi_gnt = keccak_resp_i.gnt;
    assign obi_rvalid = keccak_resp_i.rvalid;
    assign obi_rdata = keccak_resp_i.rdata;

    keccak_top #(
        .reg_req_t(reg_req_t),
        .reg_rsp_t(reg_rsp_t)
    ) keccak_top_i (
        .clk_i,
        .rst_ni,
        .reg_req_i,
        .reg_rsp_o,
        .obi_req_o(obi_req),
        .obi_gnt_i(obi_gnt),
        .obi_we_o(obi_we),
        .obi_addr_o(obi_addr),
        .obi_wdata_o(obi_wdata),
        .obi_be_o(obi_be),
        .obi_rvalid_i(obi_rvalid),
        .obi_rdata_i(obi_rdata),
        .intr_o(keccak_intr_o)
    );

endmodule
