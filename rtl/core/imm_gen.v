// imm_gen.v - immediate generator for all RV32I formats
//
// The immediate format is chosen from the opcode:
//   S: store            B: branch            U: lui / auipc
//   J: jal              I: everything else (op-imm, load, jalr)
module imm_gen (
    input  logic [31:0] instr,
    output logic [31:0] imm
);

    wire [6:0]  opcode = instr[6:0];

    wire [31:0] imm_i = {{20{instr[31]}}, instr[31:20]};
    wire [31:0] imm_s = {{20{instr[31]}}, instr[31:25], instr[11:7]};
    wire [31:0] imm_b = {{19{instr[31]}}, instr[31], instr[7], instr[30:25], instr[11:8], 1'b0};
    wire [31:0] imm_u = {instr[31:12], 12'b0};
    wire [31:0] imm_j = {{11{instr[31]}}, instr[31], instr[19:12], instr[20], instr[30:21], 1'b0};

    always_comb begin
        case (opcode)
            7'b0100011: imm = imm_s;                 // store
            7'b1100011: imm = imm_b;                 // branch
            7'b0110111,
            7'b0010111: imm = imm_u;                 // lui, auipc
            7'b1101111: imm = imm_j;                 // jal
            default:    imm = imm_i;
        endcase
    end

endmodule
