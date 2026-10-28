// hazard_unit.v - load-use hazard detection
//
// If the instruction in EX is a load whose destination is a source of the
// instruction in ID, stall IF/ID for one cycle and insert a bubble into EX.
// The loaded value is then forwarded from the MEM/WB register.
module hazard_unit (
    input  logic       id_ex_mem_read,
    input  logic [4:0] id_ex_rd,
    input  logic [4:0] if_id_rs1,
    input  logic [4:0] if_id_rs2,
    input  logic       uses_rs1,
    input  logic       uses_rs2,
    output logic       stall
);

    always_comb begin
        stall = id_ex_mem_read && id_ex_rd != 5'd0 &&
                ((uses_rs1 && id_ex_rd == if_id_rs1) ||
                 (uses_rs2 && id_ex_rd == if_id_rs2));
    end

endmodule
