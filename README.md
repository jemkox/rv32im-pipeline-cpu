# RV32I 5-Stage Pipelined CPU

A pipelined RISC-V (RV32I) processor in SystemVerilog, verified by running
programs on the RTL and on an independent C instruction-set model and
comparing the final machine state.

## Features

- 5-stage pipeline: IF, ID, EX, MEM, WB
- 45 instructions: RV32I without `fence`/`ecall`/`ebreak` (37), plus the RV32M
  multiply and divide instructions (8). See [docs/README.md](docs/README.md)
- Byte, half-word and word loads and stores
- Data forwarding from EX/MEM and MEM/WB into EX
- Load-use hazard detection with a one-cycle stall
- Branches and jumps resolved in EX (predict not taken, 2-cycle penalty)
- Register file with same-cycle write bypass
- Hand-written C toolchain: assembler, golden simulator, random program
  generator and an HDL lint tool

## Layout

```
rtl/        CPU (top.v, core/, mem/)
tb/         Verilog testbench: runs a program, dumps registers and memory
tests/      directed assembly programs
tools/      C sources: rvasm, rviss, rvgen, hdl_lint (+ lint self-test)
docs/       architecture notes
run_tests.ps1   builds the tools and runs everything
```

## Running the tests

Requirements (Windows): [Icarus Verilog](https://bleyer.org/icarus/) at
`C:\iverilog` and GCC (WinLibs).

```powershell
winget install --id Icarus.Verilog -e
winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e
powershell -ExecutionPolicy Bypass -File run_tests.ps1
```

`run_tests.ps1` builds the C tools, lints the RTL, compiles it with
`iverilog -Wall`, runs six directed programs and 50 random programs
(`-Random N` changes the count), and exits non-zero on any failure.

## How it is verified

1. `rvasm` assembles a program to a hex file.
2. The RTL testbench loads that file, runs it, and dumps all 32 registers and
   256 memory words.
3. `rviss`, a simple one-instruction-at-a-time C model, runs the same program
   and compares its final state with the RTL dump.
4. The directed tests also assert hand-computed results in the model, so the
   model itself is checked.
5. `rvgen` creates random programs with dense hazards (back-to-back
   dependencies, loads followed by uses, forward branches and jumps).

The test suite was also checked by injecting bugs into copies of the CPU
(removing forwarding paths, the load-use stall, the branch flush, breaking sign
extension, `mulh`, division by zero, and so on). All 15 behaviour-changing bugs
made the suite fail. A 16th change (removing the explicit `INT_MIN / -1` guard)
turned out to be functionally identical in Icarus, which already wraps that case
to the right answer, so no test can see it.

## Limitations

- Multiply and divide are single-cycle combinational logic. That is correct in
  simulation but would not meet timing on a real FPGA clock; hardware would need
  a multi-cycle unit that stalls the pipeline
- No CSRs, interrupts, `ecall`/`ebreak` or `fence`
- No branch prediction
- Simulation only: the design has not been synthesized or run on an FPGA

## License

MIT, see [LICENSE](LICENSE).
