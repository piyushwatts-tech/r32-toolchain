# Minimal runtime library implemented with system calls.
.text
.globl print_int
print_int:          # prints a0 as a signed decimal followed by a newline
    ecall 1
    ret
.globl print_char
print_char:         # prints the low byte of a0
    ecall 2
    ret
.globl read_int
read_int:           # returns an integer read from stdin in a0
    ecall 4
    ret
