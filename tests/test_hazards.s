# test_hazards.s - forwarding paths and load-use stalls
# Expected (checked by the model): x2=6 x3=11 x4=22 x5=16 x6=22 x7=23 x9=23
#   x11=46 x13=77 x14=5 x15=1024 x17=128 x18=22 x21=0 x23=0 x24=1
    # --- ALU to ALU forwarding (EX/MEM and MEM/WB paths)
    li    x1, 5
    addi  x2, x1, 1           # 6
    add   x3, x2, x1          # 11: x2 from EX/MEM, x1 from MEM/WB
    add   x4, x3, x3          # 22: both operands the same register
    sub   x5, x4, x2          # 16
    # --- store / load round trip with load-use stalls
    li    x10, 128
    sw    x4, 0(x10)
    lw    x6, 0(x10)          # 22
    addi  x7, x6, 1           # 23: load-use stall
    sw    x7, 4(x10)          # store data forwarded
    lw    x8, 4(x10)
    sw    x8, 8(x10)          # store of a just-loaded value: stall
    lw    x9, 8(x10)
    add   x11, x9, x9         # 46
    # --- store data forwarded from the ALU
    addi  x12, x0, 77
    sw    x12, 12(x10)
    lw    x13, 12(x10)        # 77
    # --- x0 must never be forwarded
    addi  x0, x0, 5
    add   x14, x0, x1         # 5
    # --- long dependent chain: 1 doubled ten times
    li    x15, 1
    add   x15, x15, x15
    add   x15, x15, x15
    add   x15, x15, x15
    add   x15, x15, x15
    add   x15, x15, x15
    add   x15, x15, x15
    add   x15, x15, x15
    add   x15, x15, x15
    add   x15, x15, x15
    add   x15, x15, x15       # 1024
    # --- pointer chasing: load address comes from a just-loaded value
    sw    x10, 16(x10)
    lw    x17, 16(x10)
    lw    x18, 0(x17)         # stall, then address 128 -> 22
    # --- branch on a just-loaded value
    lw    x19, 0(x10)
    li    x20, 22
    beq   x19, x20, ok1       # taken
    li    x21, 111            # must be skipped
ok1:
    # --- branch right after the instruction producing its operand
    addi  x22, x0, 3
    bne   x22, x0, ok2        # taken
    li    x23, 111            # must be skipped
ok2:
    li    x24, 1
halt:
    j     halt
