// forwarding_unit.v - bypass selection for the EX stage operands
//
// 2'b10: take the result of the instruction now in MEM (EX/MEM register)
// 2'b01: take the result of the instruction now in WB  (MEM/WB register)
// 2'b00: use the value read from the register file
//
// EX/MEM has priority because it holds the more recent result. x0 is never
// forwarded. A load in MEM is never forwarded from (its data is not ready
// yet); the hazard unit stalls the dependent instruction one cycle instead.
module forwarding_unit (
    input  logic [4:0] id_ex_rs1,
    input  logic [4:0] id_ex_rs2,
    input  logic [4:0] ex_mem_rd,
    input  logic       ex_mem_reg_write,
    input  logic [4:0] mem_wb_rd,
    input  logic       mem_wb_reg_write,
    output logic [1:0] fwd_a,
    output logic [1:0] fwd_b
);

    always_comb begin
        if (ex_mem_reg_write && ex_mem_rd != 5'd0 && ex_mem_rd == id_ex_rs1)
            fwd_a = 2'b10;
        else if (mem_wb_reg_write && mem_wb_rd != 5'd0 && mem_wb_rd == id_ex_rs1)
            fwd_a = 2'b01;
        else
            fwd_a = 2'b00;

        if (ex_mem_reg_write && ex_mem_rd != 5'd0 && ex_mem_rd == id_ex_rs2)
            fwd_b = 2'b10;
        else if (mem_wb_reg_write && mem_wb_rd != 5'd0 && mem_wb_rd == id_ex_rs2)
            fwd_b = 2'b01;
        else
            fwd_b = 2'b00;
    end

endmodule
