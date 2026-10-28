// alu.v - 32-bit ALU for RV32I and the RV32M multiply / divide extension
//
// op   operation            op   operation
//  0   ADD                  16   MUL     (low 32 bits)
//  1   SUB                  17   MULH    (signed x signed, high)
//  2   AND                  18   MULHSU  (signed x unsigned, high)
//  3   OR                   19   MULHU   (unsigned x unsigned, high)
//  4   XOR                  20   DIV
//  5   SLL                  21   DIVU
//  6   SRL                  22   REM
//  7   SRA                  23   REMU
//  8   SLT (signed)
//  9   SLTU
//
// Multiply and divide are combinational here (single cycle). That is correct
// in simulation but would not meet timing on a real FPGA clock; a hardware
// version would use a multi-cycle unit that stalls the pipeline.
// Division follows the RISC-V rules: x / 0 = all ones, x % 0 = x, and
// INT_MIN / -1 = INT_MIN with remainder 0.
module alu (
    input  logic [31:0] a,
    input  logic [31:0] b,
    input  logic [4:0]  op,
    output logic [31:0] result
);

    wire [4:0] shamt = b[4:0];

    wire signed [63:0] mul_ss = $signed(a) * $signed(b);
    wire signed [63:0] mul_su = $signed(a) * $signed({1'b0, b});
    wire        [63:0] mul_uu = a * b;

    wire [31:0] mul_lo    = mul_uu[31:0];
    wire [31:0] mul_hi_ss = mul_ss[63:32];
    wire [31:0] mul_hi_su = mul_su[63:32];
    wire [31:0] mul_hi_uu = mul_uu[63:32];

    wire        b_zero   = (b == 32'd0);
    wire        overflow = (a == 32'h80000000) && (b == 32'hFFFFFFFF);
    wire signed [31:0] sdiv = $signed(a) / $signed(b);
    wire signed [31:0] srem = $signed(a) % $signed(b);

    always_comb begin
        case (op)
            5'd0:    result = a + b;
            5'd1:    result = a - b;
            5'd2:    result = a & b;
            5'd3:    result = a | b;
            5'd4:    result = a ^ b;
            5'd5:    result = a << shamt;
            5'd6:    result = a >> shamt;
            5'd7:    result = $signed(a) >>> shamt;
            5'd8:    result = ($signed(a) < $signed(b)) ? 32'd1 : 32'd0;
            5'd9:    result = (a < b) ? 32'd1 : 32'd0;
            5'd16:   result = mul_lo;
            5'd17:   result = mul_hi_ss;
            5'd18:   result = mul_hi_su;
            5'd19:   result = mul_hi_uu;
            5'd20:   result = b_zero ? 32'hFFFFFFFF : (overflow ? a : sdiv);
            5'd21:   result = b_zero ? 32'hFFFFFFFF : (a / b);
            5'd22:   result = b_zero ? a : (overflow ? 32'd0 : srem);
            5'd23:   result = b_zero ? a : (a % b);
            default: result = 32'd0;
        endcase
    end

endmodule
