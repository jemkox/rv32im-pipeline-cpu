// instruction_memory.v - 256-word instruction ROM, word addressed by addr[9:2]
//
// The program is loaded at time zero from a hex file (one 32-bit word per
// line). The file name comes from the +PROG=<file> plusarg and defaults to
// "prog.hex". Unloaded words are NOPs (addi x0, x0, 0).
module instruction_memory (
    input  logic [31:0] addr,
    output logic [31:0] instr
);

    logic [31:0] rom [0:255];
    string       prog_file;

    initial begin
        for (int i = 0; i < 256; i = i + 1) begin
            rom[i] = 32'h00000013;
        end
        if (!$value$plusargs("PROG=%s", prog_file)) begin
            prog_file = "prog.hex";
        end
        $readmemh(prog_file, rom);
    end

    assign instr = rom[addr[9:2]];

endmodule
