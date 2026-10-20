# EXPECT: out of range
.text
.globl main
main:
    addi a0, a0, 70000
    ret
