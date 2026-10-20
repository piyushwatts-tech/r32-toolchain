# EXPECT: multiple definition of 'dup'
.text
.globl main
.globl dup
main:
    ret
dup:
    ret
