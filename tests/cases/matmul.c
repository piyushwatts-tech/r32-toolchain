extern void print_int(int);
int A[64]; int B[64]; int C[64];
void matmul(int a[], int b[], int c[], int n) {
    int i, j, k;
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            int s = 0;
            for (k = 0; k < n; k++) s += a[i * n + k] * b[k * n + j];
            c[i * n + j] = s;
        }
}
int main() {
    int i, trace = 0, total = 0;
    for (i = 0; i < 64; i++) { A[i] = (i * 7 + 3) % 11 - 5; B[i] = (i * 5 + 1) % 13 - 6; }
    matmul(A, B, C, 8);
    for (i = 0; i < 8; i++) trace += C[i * 9];
    for (i = 0; i < 64; i++) total += C[i] * (i + 1);
    print_int(trace); print_int(total);
    return 0;
}
