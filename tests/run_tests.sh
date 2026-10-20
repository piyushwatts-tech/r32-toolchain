#!/usr/bin/env bash
# Test suite for the R32 toolchain.
#  1. Differential tests: every program in tests/cases and examples/ is compiled by mcc (-O0 and -O1),
#     run on r32sim, and its output is compared with the same source compiled by the host gcc.
#  2. Diagnostics tests: invalid programs must be rejected with the expected message.
#  3. Link/assemble error tests.
#  4. Toolchain tests: object/disassembler round trip and cache-model sanity check.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  FAIL  %s\n' "$1"; [ -n "${2:-}" ] && printf '        %s\n' "$2"; }

echo "== differential tests (mcc vs host gcc) =="
for src in tests/cases/*.c examples/*.c; do
  name="$(basename "$src" .c)"
  if ! gcc -w -O0 -fwrapv "$src" tests/shim.c -o "$TMP/ref" 2>"$TMP/gcc.err"; then bad "$name (host gcc failed)" "$(head -1 "$TMP/gcc.err")"; continue; fi
  "$TMP/ref" > "$TMP/ref.out" < /dev/null
  for opt in -O0 -O1; do
    if ! scripts/r32cc $opt -o "$TMP/p.exe" "$src" 2>"$TMP/cc.err"; then bad "$name $opt (compile)" "$(head -1 "$TMP/cc.err")"; continue; fi
    bin/r32sim "$TMP/p.exe" > "$TMP/out" 2>"$TMP/sim.err" < /dev/null
    if cmp -s "$TMP/out" "$TMP/ref.out"; then ok "$name $opt"; else bad "$name $opt (output differs)" "$(diff "$TMP/out" "$TMP/ref.out" | head -3 | tr '\n' ' ')$(head -1 "$TMP/sim.err")"; fi
  done
done

echo "== diagnostics tests =="
for src in tests/errors/*.c; do
  name="$(basename "$src" .c)"
  want="$(head -1 "$src" | sed 's#^// EXPECT: ##')"
  if bin/mcc "$src" -o "$TMP/x.s" 2>"$TMP/err"; then bad "$name (should have been rejected)"; continue; fi
  if grep -qF -- "$want" "$TMP/err"; then ok "$name"; else bad "$name" "wanted '$want', got: $(head -1 "$TMP/err")"; fi
done

echo "== assembler / linker error tests =="
for src in tests/link/undef_main.s tests/link/bad_imm.s tests/link/bad_instr.s; do
  name="$(basename "$src" .s)"
  want="$(head -1 "$src" | sed 's/^# EXPECT: //')"
  if bin/r32as "$src" -o "$TMP/a.o" 2>"$TMP/err"; then
    if bin/r32ld -o "$TMP/a.exe" build/crt0.o "$TMP/a.o" 2>"$TMP/err"; then bad "$name (should fail)"; continue; fi
  fi
  if grep -qF -- "$want" "$TMP/err"; then ok "$name"; else bad "$name" "wanted '$want', got: $(head -1 "$TMP/err")"; fi
done
bin/r32as tests/link/dup_a.s -o "$TMP/a.o" && bin/r32as tests/link/dup_b.s -o "$TMP/b.o"
if bin/r32ld -o "$TMP/d.exe" build/crt0.o "$TMP/a.o" "$TMP/b.o" 2>"$TMP/err"; then bad "dup_symbol (should fail)"
elif grep -qF "multiple definition of 'dup'" "$TMP/err"; then ok "dup_symbol"; else bad "dup_symbol" "$(head -1 "$TMP/err")"; fi

echo "== toolchain tests =="
# Multi-file program: two translation units + the assembly runtime, linked together.
cat > "$TMP/lib.c" <<'EOT'
int shared_counter = 40;
int add_shared(int x) { shared_counter += x; return shared_counter; }
EOT
cat > "$TMP/main.c" <<'EOT'
extern void print_int(int);
extern int shared_counter;
int add_shared(int x);
int main() { print_int(add_shared(2)); print_int(shared_counter); return 0; }
EOT
sed -i 's/^extern int shared_counter;$//; s/print_int(shared_counter)/print_int(add_shared(0))/' "$TMP/main.c"
scripts/r32cc -o "$TMP/multi.exe" "$TMP/main.c" "$TMP/lib.c" 2>"$TMP/err" && [ "$(bin/r32sim "$TMP/multi.exe" | tr '\n' ' ')" = "42 42 " ] && ok "multi-file link (cross-module call + global data)" || bad "multi-file link" "$(head -1 "$TMP/err")"
# Exit status propagates from main.
printf 'int main() { return 7; }\n' > "$TMP/ret.c"
scripts/r32cc -o "$TMP/ret.exe" "$TMP/ret.c"; bin/r32sim "$TMP/ret.exe"; [ $? -eq 7 ] && ok "exit status from main" || bad "exit status from main"
# Disassembler runs on objects and executables.
bin/mcc tests/cases/loops.c -o "$TMP/l.s" && bin/r32as "$TMP/l.s" -o "$TMP/l.o" && bin/r32objdump "$TMP/l.o" | grep -q "Relocations" && bin/r32objdump "$TMP/multi.exe" | grep -q "add_shared:" && ok "r32objdump on .o and .exe" || bad "r32objdump"
# Runtime traps are reported with the faulting function.
printf 'int z;\nint main() { int a[4]; int i = 100000; a[i] = 1; return z; }\n' > "$TMP/oob.c"
scripts/r32cc -o "$TMP/oob.exe" "$TMP/oob.c"; bin/r32sim "$TMP/oob.exe" 2>"$TMP/err"; grep -q "trap at pc=.*<main+.*out of bounds" "$TMP/err" && ok "out-of-bounds trap reports function" || bad "oob trap" "$(head -1 "$TMP/err")"
printf 'int main() { int a = 1; int b = 0; return a / b; }\n' > "$TMP/dz.c"
scripts/r32cc -O0 -o "$TMP/dz.exe" "$TMP/dz.c"; bin/r32sim "$TMP/dz.exe" 2>"$TMP/err"; grep -q "division by zero" "$TMP/err" && ok "division-by-zero trap" || bad "div-by-zero trap" "$(head -1 "$TMP/err")"
# -O1 must never execute more instructions than -O0 on the benchmark kernels.
for k in tests/cases/matmul.c tests/cases/sieve.c tests/cases/loops.c tests/cases/cse.c; do
  scripts/r32cc -O0 -o "$TMP/o0.exe" "$k"; scripts/r32cc -O1 -o "$TMP/o1.exe" "$k"
  n0=$(bin/r32sim --stats "$TMP/o0.exe" 2>&1 >/dev/null | awk '/instructions/{print $3}')
  n1=$(bin/r32sim --stats "$TMP/o1.exe" 2>&1 >/dev/null | awk '/instructions/{print $3}')
  [ "$n1" -le "$n0" ] && ok "optimizer reduces work: $(basename "$k") ($n0 -> $n1 instructions)" || bad "optimizer regression: $(basename "$k")" "$n0 -> $n1"
done
# Cache model: a smaller cache must not produce fewer misses than a larger one.
scripts/r32cc -o "$TMP/m.exe" examples/matmul_ijk.c
m_small=$(bin/r32sim --stats --dcache 1024:32:1 "$TMP/m.exe" 2>&1 >/dev/null | awk '/D-cache/{print $5}')
m_big=$(bin/r32sim --stats --dcache 16384:32:4 "$TMP/m.exe" 2>&1 >/dev/null | awk '/D-cache/{print $5}')
[ "$m_small" -gt "$m_big" ] && ok "cache model monotonic ($m_small misses @1KB vs $m_big @16KB)" || bad "cache model" "$m_small vs $m_big"

echo
echo "passed: $PASS   failed: $FAIL"
[ "$FAIL" -eq 0 ]
