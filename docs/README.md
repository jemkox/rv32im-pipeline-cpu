# Architecture notes

## Pipeline

```
 IF            ID              EX               MEM            WB
+----+  IF/ID +----------+ ID/EX +-----------+ EX/MEM +------+ MEM/WB +----+
| PC |------->| decode   |------>| forward   |------->| data |------->| wb |
| ROM|        | regfile  |       | ALU       |        | RAM  |        | mux|
+----+        | imm gen  |       | branch    |        +------+        +----+
   ^          | hazard   |       | compare   |
   |          +----------+       +-----------+
   +------------ redirect (taken branch / jump) ---------+
```

| Stage | Work |
|-------|------|
| IF  | Fetch from the instruction ROM at `pc`; `pc + 4` or the redirect target |
| ID  | Decode, read registers, generate the immediate, detect load-use hazards |
| EX  | Forward operands, ALU, branch condition, jump target |
| MEM | Data RAM read / write |
| WB  | Select ALU result or loaded data, write the register file |

## Hazard handling

| Hazard | Resolution |
|--------|-----------|
| Result needed by the next instruction | Forward from EX/MEM into EX |
| Result needed two instructions later | Forward from MEM/WB into EX |
| Result needed three instructions later | Register file bypasses a same-cycle write |
| Load followed by a use | Stall IF and ID for one cycle, then forward from MEM/WB |
| Taken branch or any jump | Resolved in EX, the two younger instructions are squashed (2-cycle penalty) |
| Write to `x0` | Ignored; `x0` is never forwarded |

Forwarding and stall logic are in `rtl/core/forwarding_unit.v` and
`rtl/core/hazard_unit.v`. A load in the MEM stage is never forwarded from; the
stall guarantees the dependent instruction reaches EX only after the load has
reached WB.

## Instruction set

| Group | Instructions |
|-------|-------------|
| Register | `add sub sll slt sltu xor srl sra or and` |
| Immediate | `addi slti sltiu xori ori andi slli srli srai` |
| Memory | `lb lh lw lbu lhu sb sh sw` |
| Branch | `beq bne blt bge bltu bgeu` |
| Jump | `jal jalr` |
| Upper | `lui auipc` |
| Multiply / divide (RV32M) | `mul mulh mulhsu mulhu div divu rem remu` |

That is 37 RV32I instructions (everything except `fence`, `ecall`, `ebreak`) and
8 RV32M instructions. Unrecognised encodings decode as NOPs, including loads and
stores with a funct3 value that does not exist.

Division follows the RISC-V rules: `x / 0` is all ones, `x % 0` is `x`, and
`INT_MIN / -1` is `INT_MIN` with remainder 0.

## Memory map

Both memories are 256 words, word addressed by address bits `[9:2]`. Addresses
above 1 KiB wrap. Byte accesses use address bits `[1:0]`; half-word accesses use
bit `[1]` and ignore bit 0; word accesses ignore the low two bits (misaligned
accesses are not trapped). The instruction ROM is loaded at time zero from the
file given by the `+PROG=` plusarg. Unused ROM words are NOPs.

## Modules

| File | Purpose |
|------|---------|
| `rtl/top.v` | Pipeline registers and stage wiring |
| `rtl/core/alu.v` | ADD SUB AND OR XOR SLL SRL SRA SLT SLTU, plus MUL / DIV / REM (single cycle) |
| `rtl/core/register_file.v` | 32 x 32-bit, 2 read ports, write bypass |
| `rtl/core/imm_gen.v` | I, S, B, U and J immediates |
| `rtl/core/control_unit.v` | Instruction decoder |
| `rtl/core/forwarding_unit.v` | EX operand bypass selection |
| `rtl/core/hazard_unit.v` | Load-use stall detection |
| `rtl/mem/instruction_memory.v` | ROM, `$readmemh` from `+PROG` |
| `rtl/mem/data_memory.v` | RAM with byte / half-word / word access and load extension |

## Tools

| Tool | Description |
|------|-------------|
| `tools/rvasm.c` | Two-pass assembler: labels, `li`, `mv`, `call`, `ret`, `j`, `beqz`, `.word` and other common pseudo-instructions |
| `tools/rviss.c` | Golden model. `-c` compares against an RTL dump, `-e x5=55` asserts a value |
| `tools/rvgen.c` | Random program generator: forward-only control flow, so programs always end |
| `tools/hdl_lint.c` | Static checks: unbalanced `begin`/`case`/`module`, `if (posedge x)`, invalid literal digits, blocking assignments in `always_ff`, nonblocking in `always_comb`, multiple drivers, missing `default` |

`hdl_lint` is a text-level checker and cannot see port or width mismatches;
`iverilog -Wall` covers those. Its own self-test file is
`tools/lint_selftest/bad.v`; the test runner asserts it reports exactly the
planted problems.

## Writing a test

Add `tests/<name>.s`, end it with a self-jump (`halt: j halt`), and add a line to
the `$directed` table in `run_tests.ps1` listing values to assert, for example
`'x5=55'` or `'m32=0xFFFFFFCE'`. The RTL result is always compared with the
model, even with no assertions.

## Notes on the simulation counters

`top.v` exposes `cycle_count`, `stall_count` and `flush_count`, printed at the
end of each run. Programs finish by spinning in a self-jump, so the flush count
includes every iteration of that final loop and is not a measure of program
behaviour.

## Not done

- CSRs, interrupts, `ecall`
- Branch prediction
- A multi-cycle multiplier / divider that would meet FPGA timing
- FPGA synthesis and pin constraints
