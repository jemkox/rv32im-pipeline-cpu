# test_alu.s - every ALU operation, with negative operands
# Expected (checked by the model): x3=93 x4=107 x5=96 x6=0xFFFFFFFD x7=0xFFFFFF9D
#   x10=800 x11=0x1FFFFFFF x12=0xFFFFFFFF x13=1 x14=0 x15=0 x16=1
#   x27=0x1064 x28=0x12345678 x29=0x1234FFFF x31=0x80000000
    li    x1, 100
    li    x2, -7
    add   x3, x1, x2          # 93
    sub   x4, x1, x2          # 107
    and   x5, x1, x2          # 96
    or    x6, x1, x2          # -3
    xor   x7, x1, x2
    li    x9, 3
    sll   x10, x1, x9         # 800
    srl   x11, x2, x9         # logical shift of a negative value
    sra   x12, x2, x9         # arithmetic shift: -1
    slt   x13, x2, x1         # signed:   -7 < 100 -> 1
    slt   x14, x1, x2         # 0
    sltu  x15, x2, x1         # unsigned: 0xFFFFFFF9 < 100 -> 0
    sltu  x16, x1, x2         # 1
    addi  x17, x1, -1
    andi  x18, x1, 0x0f
    ori   x19, x1, 0x300
    xori  x20, x1, -1
    slli  x21, x1, 4
    srli  x22, x2, 4
    srai  x23, x2, 4
    slti  x24, x2, 0          # 1
    sltiu x25, x2, -1         # 1
    lui   x26, 0x12345
    auipc x27, 1              # pc of this instruction is 0x64
    li    x28, 0x12345678     # lui + addi
    li    x29, 0x1234FFFF     # low half is negative: tests lui rounding
    li    x30, -2048
    li    x31, 0x80000000
halt:
    j     halt
