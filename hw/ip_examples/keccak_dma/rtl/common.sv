package common;
    localparam int unsigned LANE = 64;
    localparam int unsigned PLANE = 5;
    localparam int unsigned STATE = 5;

    typedef logic [LANE - 1:0] k_lane;
    typedef k_lane [PLANE - 1:0] k_plane;
    typedef k_plane [STATE - 1:0] k_state;
endpackage
