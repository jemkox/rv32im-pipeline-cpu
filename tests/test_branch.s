# test_branch.s - all branch kinds, loops, calls, recursion with a stack
# Expected (checked by the model): x22=55 x28=0x7f x20=55 x21=21
    # --- counting loop: sum of 1..10
    li    x1, 0
    li    x2, 1
    li    x3, 11
loop:
    add   x1, x1, x2
    addi  x2, x2, 1
    blt   x2, x3, loop        # x1 = 55
    mv    x22, x1             # keep it: x1 is also ra and calls overwrite it

    # --- taken branches: each one must skip a poison instruction
    li    x28, 0
    li    x4, 5
    li    x5, 5
    li    x6, -3
    li    x7, 9
    beq   x4, x5, b1
    addi  x28, x28, 0x100
b1: ori   x28, x28, 1
    bne   x4, x7, b2
    addi  x28, x28, 0x100
b2: ori   x28, x28, 2
    blt   x6, x4, b3          # signed: -3 < 5
    addi  x28, x28, 0x100
b3: ori   x28, x28, 4
    bge   x4, x6, b4
    addi  x28, x28, 0x100
b4: ori   x28, x28, 8
    bltu  x4, x6, b5          # unsigned: 5 < 0xFFFFFFFD
    addi  x28, x28, 0x100
b5: ori   x28, x28, 16
    bgeu  x6, x4, b6
    addi  x28, x28, 0x100
b6: ori   x28, x28, 32

    # --- not-taken branches: all must fall through
    beq   x4, x7, bad
    bne   x4, x5, bad
    blt   x4, x6, bad
    bge   x6, x4, bad
    bltu  x6, x4, bad
    bgeu  x4, x6, bad
    ori   x28, x28, 64
    j     calls
bad:
    addi  x28, x28, 0x100

    # --- jalr must clear bit 0 of its target address
calls:
jt: auipc t0, 0               # t0 = address of this instruction (jt)
    addi  t0, t0, 13          # odd target: jt + 13
    jalr  x0, t0, 0           # lands on jt + 12, not jt + 13
    auipc x23, 0              # landing point (jt + 12): x23 = its own address, even

    # --- function calls
    li    a0, 10
    call  fib                 # a0 = fib(10) = 55
    mv    x20, a0
    li    sp, 960
    li    a0, 6
    call  sumto               # a0 = 6+5+4+3+2+1 = 21
    mv    x21, a0
    j     halt

fib:                          # iterative: a0 = fib(a0)
    li    t0, 0
    li    t1, 1
fib_loop:
    beqz  a0, fib_done
    add   t2, t0, t1
    mv    t0, t1
    mv    t1, t2
    addi  a0, a0, -1
    j     fib_loop
fib_done:
    mv    a0, t0
    ret

sumto:                        # recursive: a0 = a0 + (a0-1) + ... + 1
    beqz  a0, sumto_base
    addi  sp, sp, -8
    sw    ra, 0(sp)
    sw    a0, 4(sp)
    addi  a0, a0, -1
    call  sumto
    lw    t0, 4(sp)
    addi  sp, sp, 8
    add   a0, a0, t0
    lw    ra, -8(sp)
    ret                       # jalr straight after a load of ra: load-use stall
sumto_base:
    ret

halt:
    j     halt
