#!/usr/bin/env python3
"""Differential fuzzer: random MiniC programs are compiled with host gcc and with mcc (-O0/-O1)
and their outputs compared.   usage: tests/fuzz.py [count] [seed]"""
import os, random, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NV = 8

def expr(r, d):
    if d == 0 or r.random() < 0.15:
        c = r.random()
        if c < 0.55: return "v%d" % r.randrange(NV)
        if c < 0.8: return str(r.choice([0, 1, 2, 3, 7, 10, 255, 1000, 65535, 70000, -1, -5, 123456]))
        return "g[(%s) & 15]" % expr(r, 0)
    c = r.random()
    a, b = expr(r, d - 1), expr(r, d - 1)
    if c < 0.45: return "(%s %s %s)" % (a, r.choice(["+", "-", "*", "&", "|", "^"]), b)
    if c < 0.55: return "(%s %s %d)" % (a, r.choice(["<<", ">>"]), r.randrange(0, 16))
    if c < 0.65: return "(%s %s %d)" % (a, r.choice(["/", "%"]), r.choice([2, 3, 7, 10, 16, 97]))
    if c < 0.8: return "(%s %s %s)" % (a, r.choice(["<", ">", "<=", ">=", "==", "!="]), b)
    if c < 0.88: return "(%s %s %s)" % (a, r.choice(["&&", "||"]), b)
    if c < 0.92: return "(%s(%s))" % (r.choice(["-", "~", "!"]), a)
    if c < 0.96: return "f(%s, %s)" % (a, b)
    return "h(%s)" % a

def stmt(r, d, ind, depth_loop=0):
    p = "    " * ind
    c = r.random()
    if c < 0.45 or d == 0:
        if r.random() < 0.2: return "%sg[(%s) & 15] %s %s;\n" % (p, expr(r, 2), r.choice(["=", "+=", "^="]), expr(r, 3))
        return "%sv%d %s %s;\n" % (p, r.randrange(NV), r.choice(["=", "=", "+=", "-=", "*=", "^=", "|=", "&="]), expr(r, 3))
    if c < 0.7:
        s = "%sif (%s) {\n" % (p, expr(r, 2))
        for _ in range(r.randrange(1, 3)): s += stmt(r, d - 1, ind + 1, depth_loop)
        if r.random() < 0.5:
            s += "%s} else {\n" % p
            for _ in range(r.randrange(1, 3)): s += stmt(r, d - 1, ind + 1, depth_loop)
        return s + "%s}\n" % p
    if depth_loop < 2:
        iv = "i%d" % depth_loop
        s = "%sfor (%s = 0; %s < %d; %s++) {\n" % (p, iv, iv, r.randrange(1, 6), iv)
        for _ in range(r.randrange(1, 4)): s += stmt(r, d - 1, ind + 1, depth_loop + 1)
        if r.random() < 0.3: s += "%s    if (%s) break;\n" % (p, expr(r, 1))
        if r.random() < 0.3: s += "%s    if (%s) continue;\n" % (p, expr(r, 1))
        return s + "%s}\n" % p
    return "%sv%d = %s;\n" % (p, r.randrange(NV), expr(r, 2))

def program(r):
    s = "extern void print_int(int);\nint g[16];\n"
    s += "int h(int a) { return (a * 3 + 1) ^ (a >> 2); }\n"
    s += "int f(int a, int b) { int t = a + b * 2; if (t > 100) t = t - 77; return t ^ (a & 15); }\n"
    s += "int main() {\n    int " + ", ".join("v%d = %d" % (i, r.randrange(-50, 50)) for i in range(NV)) + ";\n"
    s += "    int i0, i1, i2;\n    for (i0 = 0; i0 < 16; i0++) g[i0] = i0 * 5 - 20;\n"
    for _ in range(r.randrange(6, 14)): s += stmt(r, 3, 1)
    for i in range(NV): s += "    print_int(v%d);\n" % i
    s += "    for (i0 = 0; i0 < 16; i0++) print_int(g[i0]);\n    return 0;\n}\n"
    return s

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    bad = 0
    with tempfile.TemporaryDirectory() as t:
        for k in range(n):
            r = random.Random(seed * 100003 + k)
            src = program(r)
            c = os.path.join(t, "p.c"); open(c, "w").write(src)
            if run(["gcc", "-w", "-O0", "-fwrapv", c, os.path.join(ROOT, "tests/shim.c"), "-o", os.path.join(t, "ref")]).returncode:
                print("seed %d/%d: host gcc rejected the program (fuzzer bug)" % (seed, k)); bad += 1; continue
            ref = run([os.path.join(t, "ref")]).stdout
            for opt in ("-O0", "-O1"):
                exe = os.path.join(t, "p.exe")
                cc = run([os.path.join(ROOT, "scripts/r32cc"), opt, "-o", exe, c])
                if cc.returncode:
                    print("seed %d/%d %s: compile failed: %s" % (seed, k, opt, cc.stderr.strip()[:200])); bad += 1; open("fuzz_fail_%d_%d.c" % (seed, k), "w").write(src); continue
                out = run([os.path.join(ROOT, "bin/r32sim"), exe])
                if out.stdout != ref:
                    print("seed %d/%d %s: OUTPUT MISMATCH" % (seed, k, opt)); bad += 1; open("fuzz_fail_%d_%d.c" % (seed, k), "w").write(src)
    print("fuzz: %d programs x 2 opt levels, %d failures" % (n, bad))
    sys.exit(1 if bad else 0)

main()
