RV32IM 5-Stage Pipelined CPU
A pipelined RISC-V (RV32IM) processor written in SystemVerilog and verified in simulation. Programs are run on the RTL and on an independent C instruction-set model, and the final machine states are compared.
Note on file extensions: the RTL files use the .v extension but contain SystemVerilog constructs (logic, always_ff, always_comb), so GitHub may list the language as Verilog.
Development and AI disclosure
This project was built with AI assistance:
The RTL and C tools were generated with Qwen 3.5 9B.
The design was reviewed and adjusted with Claude Sonnet 5.5.
My role was running the test suite and checking the results against the reference model.
I did not write or hand-review the RTL myself.
Features
5-stage pipeline: IF, ID, EX, MEM, WB
45 instructions: RV32I without fence/ecall/ebreak (37), plus the 8 RV32M multiply and divide instructions. See docs/README.md
Byte, half-word and word loads and stores
Data forwarding from EX/MEM and MEM/WB into EX
Load-use hazard detection with a one-cycle stall
Branches and jumps resolved in EX (predict not taken, 2-cycle penalty on a taken branch or jump)
Register file with same-cycle write bypass
Performance counters for cycles, stalls and flushes
C toolchain: assembler, golden simulator, random program generator and an HDL lint tool
Repository layout
rtl/            CPU (top.v, core/, mem/)
tb/             Testbench: runs a program, dumps registers and memory
tests/          Directed assembly programs
tools/          C sources: rvasm, rviss, rvgen, hdl_lint (+ lint self-test)
docs/           Architecture notes
run_tests.ps1   Builds the tools and runs everything
Running the tests
Requirements (Windows): Icarus Verilog at C:\iverilog and GCC (WinLibs). Icarus supports the SystemVerilog subset used in this design.
winget install --id Icarus.Verilog -e
winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e
powershell -ExecutionPolicy Bypass -File run_tests.ps1
run_tests.ps1 builds the C tools, lints the RTL, compiles it with iverilog -Wall, and runs six directed programs and 50 random programs (-Random N changes the count). It exits non-zero on any failure.
How it is verified
rvasm assembles a program to a hex file.
The RTL testbench loads that file, runs it, and dumps all 32 registers and 256 memory words.
rviss, a simple one-instruction-at-a-time C model, runs the same program and its final state is compared with the RTL dump.
The directed tests also assert hand-computed results in the model, so the model itself is checked.
rvgen creates random programs with dense hazards (back-to-back dependencies, loads followed by uses, forward branches and jumps).
The test suite was also checked by injecting bugs into copies of the CPU (removing forwarding paths, the load-use stall, the branch flush, breaking sign extension, mulh, division by zero, and so on). All 15 behaviour-changing bugs made the suite fail. A 16th change (removing the explicit INT_MIN / -1 guard) was functionally identical in Icarus, which already wraps that case to the correct result, so no test can detect it.
Limitations
Multiply and divide are single-cycle combinational logic. This is correct in simulation but would not meet timing on a real FPGA clock. Hardware would need a multi-cycle unit that stalls the pipeline.
Instruction memory is read combinationally. FPGA block RAM has a synchronous read, so a port would change the fetch timing.
No CSRs, interrupts, ecall/ebreak or fence.
No branch prediction.
No I/O peripherals (GPIO, UART, timer).
Simulation only: the design has not been synthesized or run on an FPGA.
Possible next steps
Synthesize with Yosys or a vendor tool and review the timing report
Replace the combinational multiply/divide with a multi-cycle unit that stalls the pipeline
Add memory-mapped GPIO and a UART, then run a program on an FPGA board
License
MIT, see LICENSE.