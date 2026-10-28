# test_sort.s - bubble sort of 8 signed words in memory, then sum and copy
# Expected (checked by the model): x20=120 m32=0xFFFFFFCE (smallest, -50)
#   m39=100 (largest) m48=0xFFFFFFCE (copy of m32)
    li    x10, 128            # array base (word 32)
    li    x1, 29
    sw    x1, 0(x10)
    li    x1, -4
    sw    x1, 4(x10)
    li    x1, 17
    sw    x1, 8(x10)
    li    x1, 8
    sw    x1, 12(x10)
    li    x1, 100
    sw    x1, 16(x10)
    li    x1, -50
    sw    x1, 20(x10)
    li    x1, 3
    sw    x1, 24(x10)
    li    x1, 17
    sw    x1, 28(x10)
    li    x11, 8              # n

outer:
    li    x12, 0              # swapped flag
    li    x13, 0              # i
    addi  x14, x11, -1
inner:
    slli  x15, x13, 2
    add   x15, x15, x10       # &a[i]
    lw    x16, 0(x15)
    lw    x17, 4(x15)
    ble   x16, x17, noswap    # signed compare
    sw    x17, 0(x15)
    sw    x16, 4(x15)
    li    x12, 1
noswap:
    addi  x13, x13, 1
    blt   x13, x14, inner
    bnez  x12, outer

    # --- sum of the sorted array
    li    x20, 0
    li    x13, 0
sum:
    slli  x15, x13, 2
    add   x15, x15, x10
    lw    x16, 0(x15)
    add   x20, x20, x16       # load-use stall every iteration
    addi  x13, x13, 1
    blt   x13, x11, sum

    # --- copy the sorted array to word 48
    li    x21, 192
    li    x13, 0
copy:
    slli  x15, x13, 2
    add   x18, x15, x10
    lw    x16, 0(x18)
    add   x19, x15, x21
    sw    x16, 0(x19)
    addi  x13, x13, 1
    blt   x13, x11, copy
halt:
    j     halt
