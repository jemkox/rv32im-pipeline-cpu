# test_muldiv.s - RV32M multiply and divide, including the special cases
# Expected (checked by the model):
#   x3=0xFFFFFFEB x4=0xFFFFFFFF x5=6 x6=6 x7=0xFFFFFFFF
#   x8=0xFFFFFFFE x9=1 x12=0xFFFFFFFE x13=0xFFFFFFFF x14=0 x15=7
#   x16=0xFFFFFFFF x17=7 x18=0xFFFFFFFF x19=7 x20=0x80000000 x21=0
#   x23=49 x24=56
    li    x1, 7
    li    x2, -3
    mul    x3, x1, x2         # -21
    mulh   x4, x1, x2         # high word of -21: all ones
    mulhu  x5, x1, x2         # 7 * 0xFFFFFFFD: high word = 6
    mulhsu x6, x1, x2         # signed 7 * unsigned 0xFFFFFFFD: high word = 6
    mulhsu x7, x2, x1         # signed -3 * unsigned 7 = -21: high word = -1

    div    x8, x1, x2         #  7 / -3 = -2  (rounds toward zero)
    rem    x9, x1, x2         #  7 % -3 =  1
    li     x10, -7
    li     x11, 3
    div    x12, x10, x11      # -7 / 3 = -2
    rem    x13, x10, x11      # -7 % 3 = -1
    divu   x14, x1, x2        #  7 / 0xFFFFFFFD = 0
    remu   x15, x1, x2        #  7 % 0xFFFFFFFD = 7

    # --- division by zero
    div    x16, x1, x0        # all ones
    rem    x17, x1, x0        # the dividend
    divu   x18, x1, x0        # all ones
    remu   x19, x1, x0        # the dividend

    # --- signed overflow: INT_MIN / -1
    li     x25, 0x80000000
    li     x26, -1
    div    x20, x25, x26      # INT_MIN
    rem    x21, x25, x26      # 0

    # --- result used by the very next instruction (forwarding)
    mul    x22, x1, x1        # 49
    addi   x23, x22, 0        # 49: uses the mul result immediately
    add    x24, x23, x1       # 56
halt:
    j      halt
