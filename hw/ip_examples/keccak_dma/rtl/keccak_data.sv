import common::*;

module keccak_data (
        input clk,
        input rst_n,
        input start,
        input [1599:0] Din,
        output ready,
        output [1599:0] Dout
);
    k_state reg_data, round_in, round_out;
    logic [4:0] counter_rounds;
    logic [LANE-1:0] round_constant_signal;
    logic compute_permutation, permutation_computed;

    keccak_round_constants inst_round_constants (
            .round_number  (counter_rounds),
            .round_constant(round_constant_signal)
    );

    keccak_round inst_round (
            .Round_in(round_in),
            .Round_constant_signal(round_constant_signal),
            .Round_out(round_out)
    );

    genvar y, x, i;
    generate
        for (y = 0; y < 5; y++) begin : g_y
            for (x = 0; x < 5; x++) begin : g_x
                for (i = 0; i < LANE; i++) begin : g_bit
                    assign Dout[320*y+64*x+i] = reg_data[y][x][i];
                end
            end
        end
    endgenerate

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_data <= '0;
            counter_rounds <= '0;
            permutation_computed <= 1'b1;
            compute_permutation <= 1'b0;
        end else begin
            if (start) begin
                reg_data <= '0;
                counter_rounds <= '0;
                compute_permutation <= 1'b1;
                permutation_computed <= 1'b1;
            end else begin
                if (compute_permutation && permutation_computed) begin
                    counter_rounds <= 5'b0_0001;
                    permutation_computed <= 1'b0;
                    reg_data <= round_out;
                end else begin
                    if ((counter_rounds < 24) && !permutation_computed) begin
                        counter_rounds <= counter_rounds + 1;
                        reg_data <= round_out;
                    end

                    if (counter_rounds == 23) begin
                        permutation_computed <= 1'b1;
                        compute_permutation <= 1'b0;
                        counter_rounds <= '0;
                    end
                end
            end
        end
    end

    generate
        for (y = 0; y < 5; y++) begin : g_y_in
            for (x = 0; x < 5; x++) begin : g_x_in
                for (i = 0; i < LANE; i++) begin : g_bit_in
                    assign round_in[y][x][i] = reg_data[y][x][i] ^ (Din[320*y+64*x+i] & permutation_computed);
                end
            end
        end
    endgenerate

    assign ready = permutation_computed;
endmodule
