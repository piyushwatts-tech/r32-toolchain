# Program entry: call main, then exit with its return value (already in a0).
.text
.globl _start
_start:
    call main
    ecall 3
