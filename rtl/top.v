// top.v - 5-stage pipelined RV32IM CPU
//
//   IF -> ID -> EX -> MEM -> WB
//
// Hazard handling
//   * Data hazards are resolved by forwarding into EX from the EX/MEM and
//     MEM/WB registers (forwarding_unit).
//   * A load followed immediately by a user of the loaded value stalls IF/ID
//     one cycle (hazard_unit); the value is then forwarded from MEM/WB.
//   * Branches and jumps are resolved in EX, with predict-not-taken. A taken
//     branch or any jump redirects the PC and squashes the two younger
//     instructions (2-cycle penalty).
//   * The register file bypasses a same-cycle write to its read ports.
//
// Instruction set: RV32I without fence, ecall and ebreak (37 instructions),
// plus the RV32M multiply / divide instructions (8 instructions).
`include "core/alu.v"
`include "core/register_file.v"
`include "core/imm_gen.v"
`include "core/control_unit.v"
`include "core/forwarding_unit.v"
`include "core/hazard_unit.v"
`include "mem/instruction_memory.v"
`include "mem/data_memory.v"

module top (
    input  logic        clk,
    input  logic        reset,
    output logic [31:0] pc_out
);

    localparam logic [31:0] NOP = 32'h00000013;   // addi x0, x0, 0

    // ------------------------------------------------------------------
    // Signal declarations
    // ------------------------------------------------------------------
    // IF
    logic [31:0] pc;
    logic [31:0] instr_f;
    logic        stall;
    logic        redirect;
    logic [31:0] redirect_target;

    // IF/ID
    logic [31:0] if_id_pc;
    logic [31:0] if_id_instr;

    // ID
    logic [31:0] id_rd1, id_rd2, id_imm;
    logic        c_reg_write, c_alu_src_b, c_a_pc, c_a_zero;
    logic [4:0]  c_alu_op;
    logic        c_mem_read, c_mem_write, c_branch, c_jal, c_jalr;
    logic        c_uses_rs1, c_uses_rs2;
    wire  [4:0]  id_rs1 = if_id_instr[19:15];
    wire  [4:0]  id_rs2 = if_id_instr[24:20];
    wire  [4:0]  id_rd  = if_id_instr[11:7];
    wire         kill   = redirect | stall;       // turn ID instruction into a bubble

    // ID/EX
    logic [31:0] id_ex_pc, id_ex_rd1, id_ex_rd2, id_ex_imm;
    logic [4:0]  id_ex_rs1, id_ex_rs2, id_ex_rd;
    logic [2:0]  id_ex_funct3;
    logic [4:0]  id_ex_alu_op;
    logic        id_ex_reg_write, id_ex_alu_src_b, id_ex_a_pc, id_ex_a_zero;
    logic        id_ex_mem_read, id_ex_mem_write;
    logic        id_ex_branch, id_ex_jal, id_ex_jalr;

    // EX
    logic [1:0]  fwd_a, fwd_b;
    logic [31:0] ex_a_reg, ex_b_reg;               // operands after forwarding
    logic [31:0] ex_alu_a, ex_alu_b, ex_alu_result, ex_result;
    logic        br_eq, br_lt, br_ltu, br_cond;

    // EX/MEM
    logic [31:0] ex_mem_alu_result, ex_mem_store_data;
    logic [4:0]  ex_mem_rd;
    logic [2:0]  ex_mem_funct3;
    logic        ex_mem_reg_write, ex_mem_mem_read, ex_mem_mem_write;

    // MEM
    logic [31:0] mem_rdata;

    // MEM/WB
    logic [31:0] mem_wb_alu_result, mem_wb_rdata;
    logic [4:0]  mem_wb_rd;
    logic        mem_wb_reg_write, mem_wb_mem_read;

    // WB
    logic [31:0] wb_data;

    // Performance counters (visible to the testbench)
    logic [31:0] cycle_count, stall_count, flush_count;

    assign pc_out = pc;

    // ------------------------------------------------------------------
    // IF: program counter and instruction fetch
    // ------------------------------------------------------------------
    always_ff @(posedge clk or posedge reset) begin
        if (reset)         pc <= 32'd0;
        else if (redirect) pc <= redirect_target;
        else if (!stall)   pc <= pc + 32'd4;
    end

    instruction_memory u_imem (
        .addr  (pc),
        .instr (instr_f)
    );

    always_ff @(posedge clk or posedge reset) begin
        if (reset) begin
            if_id_pc    <= 32'd0;
            if_id_instr <= NOP;
        end else if (redirect) begin
            if_id_pc    <= 32'd0;
            if_id_instr <= NOP;
        end else if (!stall) begin
            if_id_pc    <= pc;
            if_id_instr <= instr_f;
        end
    end

    // ------------------------------------------------------------------
    // ID: decode, register read, hazard detection
    // ------------------------------------------------------------------
    register_file u_rf (
        .clk        (clk),
        .reset      (reset),
        .rs1        (id_rs1),
        .rs2        (id_rs2),
        .rd         (mem_wb_rd),
        .write_data (wb_data),
        .we         (mem_wb_reg_write),
        .rd1        (id_rd1),
        .rd2        (id_rd2)
    );

    control_unit u_cu (
        .instr     (if_id_instr),
        .reg_write (c_reg_write),
        .alu_src_b (c_alu_src_b),
        .a_pc      (c_a_pc),
        .a_zero    (c_a_zero),
        .alu_op    (c_alu_op),
        .mem_read  (c_mem_read),
        .mem_write (c_mem_write),
        .branch    (c_branch),
        .jal       (c_jal),
        .jalr      (c_jalr),
        .uses_rs1  (c_uses_rs1),
        .uses_rs2  (c_uses_rs2)
    );

    imm_gen u_imm (
        .instr (if_id_instr),
        .imm   (id_imm)
    );

    hazard_unit u_hazard (
        .id_ex_mem_read (id_ex_mem_read),
        .id_ex_rd       (id_ex_rd),
        .if_id_rs1      (id_rs1),
        .if_id_rs2      (id_rs2),
        .uses_rs1       (c_uses_rs1),
        .uses_rs2       (c_uses_rs2),
        .stall          (stall)
    );

    // ------------------------------------------------------------------
    // ID/EX register. Only signals with side effects are cleared for a bubble.
    // ------------------------------------------------------------------
    always_ff @(posedge clk or posedge reset) begin
        if (reset) begin
            id_ex_pc        <= 32'd0;
            id_ex_rd1       <= 32'd0;
            id_ex_rd2       <= 32'd0;
            id_ex_imm       <= 32'd0;
            id_ex_rs1       <= 5'd0;
            id_ex_rs2       <= 5'd0;
            id_ex_rd        <= 5'd0;
            id_ex_funct3    <= 3'd0;
            id_ex_alu_op    <= 5'd0;
            id_ex_reg_write <= 1'b0;
            id_ex_alu_src_b <= 1'b0;
            id_ex_a_pc      <= 1'b0;
            id_ex_a_zero    <= 1'b0;
            id_ex_mem_read  <= 1'b0;
            id_ex_mem_write <= 1'b0;
            id_ex_branch    <= 1'b0;
            id_ex_jal       <= 1'b0;
            id_ex_jalr      <= 1'b0;
        end else begin
            id_ex_pc        <= if_id_pc;
            id_ex_rd1       <= id_rd1;
            id_ex_rd2       <= id_rd2;
            id_ex_imm       <= id_imm;
            id_ex_rs1       <= id_rs1;
            id_ex_rs2       <= id_rs2;
            id_ex_rd        <= id_rd;
            id_ex_funct3    <= if_id_instr[14:12];
            id_ex_alu_op    <= c_alu_op;
            id_ex_alu_src_b <= c_alu_src_b;
            id_ex_a_pc      <= c_a_pc;
            id_ex_a_zero    <= c_a_zero;
            id_ex_reg_write <= kill ? 1'b0 : c_reg_write;
            id_ex_mem_read  <= kill ? 1'b0 : c_mem_read;
            id_ex_mem_write <= kill ? 1'b0 : c_mem_write;
            id_ex_branch    <= kill ? 1'b0 : c_branch;
            id_ex_jal       <= kill ? 1'b0 : c_jal;
            id_ex_jalr      <= kill ? 1'b0 : c_jalr;
        end
    end

    // ------------------------------------------------------------------
    // EX: forwarding, ALU, branch / jump resolution
    // ------------------------------------------------------------------
    forwarding_unit u_fwd (
        .id_ex_rs1        (id_ex_rs1),
        .id_ex_rs2        (id_ex_rs2),
        .ex_mem_rd        (ex_mem_rd),
        .ex_mem_reg_write (ex_mem_reg_write),
        .mem_wb_rd        (mem_wb_rd),
        .mem_wb_reg_write (mem_wb_reg_write),
        .fwd_a            (fwd_a),
        .fwd_b            (fwd_b)
    );

    always_comb begin
        case (fwd_a)
            2'b10:   ex_a_reg = ex_mem_alu_result;
            2'b01:   ex_a_reg = wb_data;
            default: ex_a_reg = id_ex_rd1;
        endcase

        case (fwd_b)
            2'b10:   ex_b_reg = ex_mem_alu_result;
            2'b01:   ex_b_reg = wb_data;
            default: ex_b_reg = id_ex_rd2;
        endcase
    end

    assign ex_alu_a = id_ex_a_zero ? 32'd0 : (id_ex_a_pc ? id_ex_pc : ex_a_reg);
    assign ex_alu_b = id_ex_alu_src_b ? id_ex_imm : ex_b_reg;

    alu u_alu (
        .a      (ex_alu_a),
        .b      (ex_alu_b),
        .op     (id_ex_alu_op),
        .result (ex_alu_result)
    );

    // jal / jalr write the return address (pc + 4) to rd
    assign ex_result = (id_ex_jal | id_ex_jalr) ? (id_ex_pc + 32'd4) : ex_alu_result;

    // branch condition on the forwarded register values
    assign br_eq  = (ex_a_reg == ex_b_reg);
    assign br_lt  = ($signed(ex_a_reg) < $signed(ex_b_reg));
    assign br_ltu = (ex_a_reg < ex_b_reg);

    always_comb begin
        case (id_ex_funct3)
            3'b000:  br_cond = br_eq;     // beq
            3'b001:  br_cond = !br_eq;    // bne
            3'b100:  br_cond = br_lt;     // blt
            3'b101:  br_cond = !br_lt;    // bge
            3'b110:  br_cond = br_ltu;    // bltu
            3'b111:  br_cond = !br_ltu;   // bgeu
            default: br_cond = 1'b0;
        endcase
    end

    assign redirect = (id_ex_branch & br_cond) | id_ex_jal | id_ex_jalr;

    // jalr target is rs1 + imm with bit 0 cleared; branch / jal are pc-relative
    assign redirect_target = id_ex_jalr ? {ex_alu_result[31:1], 1'b0}
                                        : (id_ex_pc + id_ex_imm);

    // ------------------------------------------------------------------
    // EX/MEM register
    // ------------------------------------------------------------------
    always_ff @(posedge clk or posedge reset) begin
        if (reset) begin
            ex_mem_alu_result <= 32'd0;
            ex_mem_store_data <= 32'd0;
            ex_mem_rd         <= 5'd0;
            ex_mem_funct3     <= 3'd0;
            ex_mem_reg_write  <= 1'b0;
            ex_mem_mem_read   <= 1'b0;
            ex_mem_mem_write  <= 1'b0;
        end else begin
            ex_mem_alu_result <= ex_result;
            ex_mem_store_data <= ex_b_reg;
            ex_mem_rd         <= id_ex_rd;
            ex_mem_funct3     <= id_ex_funct3;
            ex_mem_reg_write  <= id_ex_reg_write;
            ex_mem_mem_read   <= id_ex_mem_read;
            ex_mem_mem_write  <= id_ex_mem_write;
        end
    end

    // ------------------------------------------------------------------
    // MEM
    // ------------------------------------------------------------------
    data_memory u_dmem (
        .clk   (clk),
        .addr  (ex_mem_alu_result),
        .wdata (ex_mem_store_data),
        .we    (ex_mem_mem_write),
        .re    (ex_mem_mem_read),
        .funct3(ex_mem_funct3),
        .rdata (mem_rdata)
    );

    always_ff @(posedge clk or posedge reset) begin
        if (reset) begin
            mem_wb_alu_result <= 32'd0;
            mem_wb_rdata      <= 32'd0;
            mem_wb_rd         <= 5'd0;
            mem_wb_reg_write  <= 1'b0;
            mem_wb_mem_read   <= 1'b0;
        end else begin
            mem_wb_alu_result <= ex_mem_alu_result;
            mem_wb_rdata      <= mem_rdata;
            mem_wb_rd         <= ex_mem_rd;
            mem_wb_reg_write  <= ex_mem_reg_write;
            mem_wb_mem_read   <= ex_mem_mem_read;
        end
    end

    // ------------------------------------------------------------------
    // WB
    // ------------------------------------------------------------------
    assign wb_data = mem_wb_mem_read ? mem_wb_rdata : mem_wb_alu_result;

    // ------------------------------------------------------------------
    // Performance counters
    // ------------------------------------------------------------------
    always_ff @(posedge clk or posedge reset) begin
        if (reset) begin
            cycle_count <= 32'd0;
            stall_count <= 32'd0;
            flush_count <= 32'd0;
        end else begin
            cycle_count <= cycle_count + 32'd1;
            if (stall)    stall_count <= stall_count + 32'd1;
            if (redirect) flush_count <= flush_count + 32'd1;
        end
    end

endmodule
