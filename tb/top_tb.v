// top_tb.v - runs a program on the CPU and dumps the final state
//
// The testbench does not judge the result itself. It writes all registers and
// all data memory words to a text file, and tools/rviss (the C golden model)
// compares that file against its own execution of the same program.
//
// Plusargs
//   +PROG=<file>    program hex file (read by instruction_memory)
//   +DUMP=<file>    state dump file          (default rtl_dump.txt)
//   +CYCLES=<n>     clock cycles to run      (default 6000)
`include "top.v"

module top_tb;

    logic        clk;
    logic        reset;
    wire  [31:0] pc_out;

    integer cycles;
    integer fd;
    string  dump_file;

    top dut (
        .clk    (clk),
        .reset  (reset),
        .pc_out (pc_out)
    );

    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    initial begin
        if (!$value$plusargs("CYCLES=%d", cycles)) cycles = 6000;
        if (!$value$plusargs("DUMP=%s", dump_file)) dump_file = "rtl_dump.txt";

        reset = 1'b1;
        #22;
        reset = 1'b0;

        repeat (cycles) @(posedge clk);
        #1;

        fd = $fopen(dump_file, "w");
        for (int i = 0; i < 32; i = i + 1) begin
            $fdisplay(fd, "x%0d %08x", i, dut.u_rf.rf[i]);
        end
        for (int i = 0; i < 256; i = i + 1) begin
            $fdisplay(fd, "m%0d %08x", i, dut.u_dmem.mem[i]);
        end
        $fclose(fd);

        $display("RTL: %0d cycles run, %0d load-use stalls, %0d control-flow redirects",
                 dut.cycle_count, dut.stall_count, dut.flush_count);
        $finish;
    end

endmodule
