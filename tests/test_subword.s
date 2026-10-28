# test_subword.s - byte and half-word loads and stores
# Memory word at x10 holds 0x8081FF7F, i.e. bytes 7F FF 81 80 (low to high).
# Expected (checked by the model): x2=127 x3=0xFFFFFFFF x4=0xFFFFFF81
#   x5=0xFFFFFF80 x6=255 x7=128 x8=0xFFFFFF7F x9=0xFFFF8081 x11=0xFF7F
#   x12=0x8081 x15=0xBEEFCD7F x16=0xCDEFBEEF x17=0xFFFFFFCD x18=0xFFFFFFCE
#   m64=0xCDEFBEEF m65=0xCDCD
    li    x10, 256            # word 64
    li    x1, 0x8081FF7F
    sw    x1, 0(x10)

    # --- loads with sign / zero extension at every byte offset
    lb    x2, 0(x10)          #  0x7F -> 127
    lb    x3, 1(x10)          #  0xFF -> -1
    lb    x4, 2(x10)          #  0x81 -> sign extended
    lb    x5, 3(x10)          #  0x80 -> sign extended
    lbu   x6, 1(x10)          #  255
    lbu   x7, 3(x10)          #  128
    lh    x8, 0(x10)          #  0xFF7F -> sign extended
    lh    x9, 2(x10)          #  0x8081 -> sign extended
    lhu   x11, 0(x10)
    lhu   x12, 2(x10)

    # --- partial stores merge into the existing word
    li    x13, 0x1234ABCD
    li    x14, 0x5678BEEF
    sb    x13, 1(x10)         # byte 1 = CD   -> 0x8081CD7F
    sh    x14, 2(x10)         # upper half = BEEF -> 0xBEEFCD7F
    lw    x15, 0(x10)
    sb    x13, 3(x10)         # byte 3 = CD   -> 0xCDEFCD7F
    sh    x14, 0(x10)         # lower half = BEEF -> 0xCDEFBEEF
    lw    x16, 0(x10)

    # --- load-use on a sub-word load
    lb    x17, 3(x10)         # 0xCD -> 0xFFFFFFCD
    addi  x18, x17, 1         # 0xFFFFFFCE

    # --- two byte stores into a fresh word build 0xCDCD
    sb    x13, 4(x10)
    sb    x13, 5(x10)
halt:
    j     halt
