extern void print_int(int);
int id(int x) { return x; }
int many(int a) {
    int v0 = a + 0, v1 = a + 1, v2 = a + 2, v3 = a + 3, v4 = a + 4, v5 = a + 5, v6 = a + 6, v7 = a + 7;
    int v8 = a + 8, v9 = a + 9, v10 = a + 10, v11 = a + 11, v12 = a + 12, v13 = a + 13, v14 = a + 14, v15 = a + 15;
    int v16 = a + 16, v17 = a + 17, v18 = a + 18, v19 = a + 19, v20 = a + 20, v21 = a + 21, v22 = a + 22, v23 = a + 23;
    int v24 = a + 24, v25 = a + 25, v26 = a + 26, v27 = a + 27, v28 = a + 28, v29 = a + 29;
    int s = v0 * v29 + v1 * v28 + v2 * v27 + v3 * v26 + v4 * v25 + v5 * v24 + v6 * v23 + v7 * v22;
    s += v8 * v21 + v9 * v20 + v10 * v19 + v11 * v18 + v12 * v17 + v13 * v16 + v14 * v15;
    print_int(s);
    return s + v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12 + v13 + v14 + v15 + v16 + v17 + v18 + v19 + v20 + v21 + v22 + v23 + v24 + v25 + v26 + v27 + v28 + v29;
}
int acrosscalls(int n) {
    int a = n * 2, b = n * 3, c = n * 5, i, t = 0;
    for (i = 0; i < 4; i++) {
        t += id(a) + id(b) * i + id(c);
        a += 1; b += 2; c += 3;
    }
    return t + a + b + c;
}
int main() {
    print_int(many(1)); print_int(many(-7)); print_int(acrosscalls(3)); print_int(acrosscalls(100));
    return 0;
}
