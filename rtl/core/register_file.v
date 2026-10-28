// register_file.v - 32 x 32-bit registers, 2 read ports, 1 write port
//
// Asynchronous active-high reset. x0 always reads as zero and cannot be
// written. A write in the current cycle is bypassed to the read ports, so an
// instruction in ID sees the value being written back in the same cycle.
module register_file (
    input  logic        clk,
    input  logic        reset,
    input  logic [4:0]  rs1,
    input  logic [4:0]  rs2,
    input  logic [4:0]  rd,
    input  logic [31:0] write_data,
    input  logic        we,
    output logic [31:0] rd1,
    output logic [31:0] rd2
);

    logic [31:0] rf [0:31];

    always_ff @(posedge clk or posedge reset) begin
        if (reset) begin
            for (int i = 0; i < 32; i = i + 1) begin
                rf[i] <= 32'd0;
            end
        end else if (we && rd != 5'd0) begin
            rf[rd] <= write_data;
        end
    end

    always_comb begin
        if (rs1 == 5'd0)                          rd1 = 32'd0;
        else if (we && rd == rs1)                 rd1 = write_data;
        else                                      rd1 = rf[rs1];

        if (rs2 == 5'd0)                          rd2 = 32'd0;
        else if (we && rd == rs2)                 rd2 = write_data;
        else                                      rd2 = rf[rs2];
    end

endmodule
