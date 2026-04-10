import common::*;

module keccak_round (
        input k_state Round_in,
        input [LANE - 1:0] Round_constant_signal,
        output k_state Round_out
);
    k_state theta_in, theta_out;
    k_state rho_in, rho_out;
    k_state pi_in, pi_out;
    k_state chi_in, chi_out;
    k_state iota_in, iota_out;

    k_plane sum_sheet;

    assign theta_in = Round_in;
    assign rho_in = theta_out;
    assign pi_in = rho_out;
    assign chi_in = pi_out;
    assign iota_in = chi_out;
    assign Round_out = iota_out;

    genvar y, x, i;

    // Theta
    generate
        for (x = 0; x <= 4; x++)
        for (i = 0; i <= LANE - 1; i++)
            assign sum_sheet[x][i] = theta_in[0][x][i] ^ theta_in[1][x][i] ^ theta_in[2][x][i] ^ theta_in[3][x][i] ^ theta_in[4][x][i];
    endgenerate

    generate
        for (y = 0; y <= 4; y++) begin : loop_y
            for (x = 0; x <= 4; x++) begin : loop_x
                for (i = 0; i <= LANE - 1; i++) begin : loop_i
                    assign theta_out[y][x][i] = theta_in[y][x][i] ^ 
                                                                                                sum_sheet[(x + 4) % 5][i] ^ 
                                                                                                sum_sheet[(x + 1) % 5][(i + LANE - 1) % LANE];
                end
            end
        end
    endgenerate

    // Rho
    always_comb begin : Rho_shift
        for (int ri = 0; ri < LANE; ri++) begin
            rho_out[0][0][ri] = rho_in[0][0][ri];
            rho_out[0][1][ri] = rho_in[0][1][(ri+63)%LANE];
            rho_out[0][2][ri] = rho_in[0][2][(ri+2)%LANE];
            rho_out[0][3][ri] = rho_in[0][3][(ri+36)%LANE];
            rho_out[0][4][ri] = rho_in[0][4][(ri+37)%LANE];

            rho_out[1][0][ri] = rho_in[1][0][(ri+28)%LANE];
            rho_out[1][1][ri] = rho_in[1][1][(ri+20)%LANE];
            rho_out[1][2][ri] = rho_in[1][2][(ri+58)%LANE];
            rho_out[1][3][ri] = rho_in[1][3][(ri+9)%LANE];
            rho_out[1][4][ri] = rho_in[1][4][(ri+44)%LANE];

            rho_out[2][0][ri] = rho_in[2][0][(ri+61)%LANE];
            rho_out[2][1][ri] = rho_in[2][1][(ri+54)%LANE];
            rho_out[2][2][ri] = rho_in[2][2][(ri+21)%LANE];
            rho_out[2][3][ri] = rho_in[2][3][(ri+39)%LANE];
            rho_out[2][4][ri] = rho_in[2][4][(ri+25)%LANE];

            rho_out[3][0][ri] = rho_in[3][0][(ri+23)%LANE];
            rho_out[3][1][ri] = rho_in[3][1][(ri+19)%LANE];
            rho_out[3][2][ri] = rho_in[3][2][(ri+49)%LANE];
            rho_out[3][3][ri] = rho_in[3][3][(ri+43)%LANE];
            rho_out[3][4][ri] = rho_in[3][4][(ri+56)%LANE];

            rho_out[4][0][ri] = rho_in[4][0][(ri+46)%LANE];
            rho_out[4][1][ri] = rho_in[4][1][(ri+62)%LANE];
            rho_out[4][2][ri] = rho_in[4][2][(ri+3)%LANE];
            rho_out[4][3][ri] = rho_in[4][3][(ri+8)%LANE];
            rho_out[4][4][ri] = rho_in[4][4][(ri+50)%LANE];
        end
    end

    // Pi
    generate
        for (y = 0; y <= 4; y++)
        for (x = 0; x <= 4; x++)
        for (i = 0; i <= LANE - 1; i++) assign pi_out[(2*x+3*y)%5][0*x+1*y][i] = pi_in[y][x][i];
    endgenerate

    // Chi
    generate
        for (y = 0; y <= 4; y++)
        for (x = 0; x <= 4; x++)
        for (i = 0; i <= LANE - 1; i++)
            assign chi_out[y][x][i] = chi_in[y][x][i] ^ ((~chi_in[y][(x+1) % 5][i]) & chi_in[y][(x+2) % 5][i]);
    endgenerate

    // Iota
    generate
        for (y = 0; y <= 4; y++)
        for (x = 0; x <= 4; x++)
        for (i = 0; i <= LANE - 1; i++)
            assign iota_out[y][x][i] = (y == 0 && x == 0) ? (iota_in[y][x][i] ^ Round_constant_signal[i]) : iota_in[y][x][i];
    endgenerate
endmodule
