// control_unit.v - instruction decoder
//
// Supports RV32I (except fence and system instructions) and the RV32M
// multiply / divide instructions. Unrecognised encodings, including load /
// store funct3 values that do not exist, decode to a NOP (no register, memory
// or PC effect).
module control_unit (
    input  logic [31:0] instr,
    output logic        reg_write,
    output logic        alu_src_b,   // 1: second ALU operand is the immediate
    output logic        a_pc,        // 1: first ALU operand is the PC (auipc)
    output logic        a_zero,      // 1: first ALU operand is zero (lui)
    output logic [4:0]  alu_op,
    output logic        mem_read,
    output logic        mem_write,
    output logic        branch,
    output logic        jal,
    output logic        jalr,
    output logic        uses_rs1,
    output logic        uses_rs2
);

    wire [6:0] opcode   = instr[6:0];
    wire [2:0] funct3   = instr[14:12];
    wire       funct7_5 = instr[30];
    wire       m_ext    = (instr[31:25] == 7'b0000001);

    // loads: lb lh lw lbu lhu    stores: sb sh sw
    wire valid_load  = (funct3 == 3'b000) || (funct3 == 3'b001) || (funct3 == 3'b010) ||
                       (funct3 == 3'b100) || (funct3 == 3'b101);
    wire valid_store = (funct3 == 3'b000) || (funct3 == 3'b001) || (funct3 == 3'b010);

    // ALU operation for register-register and register-immediate instructions
    logic [4:0] alu_fn_base;
    logic [4:0] alu_fn_r;
    logic [4:0] alu_fn_i;

    always_comb begin
        case (funct3)
            3'b000:  alu_fn_base = funct7_5 ? 5'd1 : 5'd0;   // sub / add
            3'b001:  alu_fn_base = 5'd5;                     // sll
            3'b010:  alu_fn_base = 5'd8;                     // slt
            3'b011:  alu_fn_base = 5'd9;                     // sltu
            3'b100:  alu_fn_base = 5'd4;                     // xor
            3'b101:  alu_fn_base = funct7_5 ? 5'd7 : 5'd6;   // sra / srl
            3'b110:  alu_fn_base = 5'd3;                     // or
            default: alu_fn_base = 5'd2;                     // and
        endcase

        // RV32M: funct7 = 0000001 selects mul / div, funct3 picks which.
        // Applies to register-register instructions only; in OP-IMM those
        // bits are part of the immediate.
        alu_fn_r = m_ext ? {2'b10, funct3} : alu_fn_base;

        // addi has no subtract form; bit 30 is part of the immediate there
        alu_fn_i = (funct3 == 3'b000) ? 5'd0 : alu_fn_base;
    end

    always_comb begin
        reg_write = 1'b0;
        alu_src_b = 1'b0;
        a_pc      = 1'b0;
        a_zero    = 1'b0;
        alu_op    = 5'd0;
        mem_read  = 1'b0;
        mem_write = 1'b0;
        branch    = 1'b0;
        jal       = 1'b0;
        jalr      = 1'b0;
        uses_rs1  = 1'b0;
        uses_rs2  = 1'b0;

        case (opcode)
            7'b0110011: begin                              // R-type (and RV32M)
                reg_write = 1'b1;
                alu_op    = alu_fn_r;
                uses_rs1  = 1'b1;
                uses_rs2  = 1'b1;
            end
            7'b0010011: begin                              // OP-IMM
                reg_write = 1'b1;
                alu_src_b = 1'b1;
                alu_op    = alu_fn_i;
                uses_rs1  = 1'b1;
            end
            7'b0000011: begin                              // loads
                if (valid_load) begin
                    reg_write = 1'b1;
                    alu_src_b = 1'b1;
                    mem_read  = 1'b1;
                    uses_rs1  = 1'b1;
                end
            end
            7'b0100011: begin                              // stores
                if (valid_store) begin
                    alu_src_b = 1'b1;
                    mem_write = 1'b1;
                    uses_rs1  = 1'b1;
                    uses_rs2  = 1'b1;
                end
            end
            7'b1100011: begin                              // branches
                branch    = 1'b1;
                uses_rs1  = 1'b1;
                uses_rs2  = 1'b1;
            end
            7'b1101111: begin                              // JAL
                reg_write = 1'b1;
                jal       = 1'b1;
            end
            7'b1100111: begin                              // JALR
                reg_write = 1'b1;
                alu_src_b = 1'b1;
                jalr      = 1'b1;
                uses_rs1  = 1'b1;
            end
            7'b0110111: begin                              // LUI
                reg_write = 1'b1;
                alu_src_b = 1'b1;
                a_zero    = 1'b1;
            end
            7'b0010111: begin                              // AUIPC
                reg_write = 1'b1;
                alu_src_b = 1'b1;
                a_pc      = 1'b1;
            end
            default: ;
        endcase
    end

endmodule
