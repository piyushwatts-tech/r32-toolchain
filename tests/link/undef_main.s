# EXPECT: undefined reference to 'missing_fn'
.text
.globl main
main:
    call missing_fn
    ret
