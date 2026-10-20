/* Fixed-point (Q8) multi-layer perceptron inference: 16 -> 16 (ReLU) -> 4.
 * Weights are pseudo-random (NOT trained); the point is to compile and profile typical
 * ML inference kernels (dense layer, ReLU, argmax) with integer-only arithmetic.  */
extern void print_int(int);
int W1[256]; int b1[16]; int W2[64]; int b2[4];
int x[16]; int h[16]; int y[4];
int seed = 7;
int lcg() { seed = seed * 1103515245 + 12345; return (seed >> 16) & 32767; }
void init(int w[], int n, int range) { int i; for (i = 0; i < n; i++) w[i] = lcg() % (2 * range + 1) - range; }
void dense(int in[], int w[], int b[], int out[], int n_in, int n_out, int relu) {
    int o, i;
    for (o = 0; o < n_out; o++) {
        int acc = b[o] * 256;
        for (i = 0; i < n_in; i++) acc += in[i] * w[o * n_in + i];
        acc = acc >> 8;
        if (relu && acc < 0) acc = 0;
        out[o] = acc;
    }
}
int argmax(int v[], int n) {
    int i, best = 0;
    for (i = 1; i < n; i++) if (v[i] > v[best]) best = i;
    return best;
}
int main() {
    int sample, i, cls;
    init(W1, 256, 40); init(b1, 16, 20); init(W2, 64, 40); init(b2, 4, 20);
    for (sample = 0; sample < 6; sample++) {
        for (i = 0; i < 16; i++) x[i] = lcg() % 256;
        dense(x, W1, b1, h, 16, 16, 1);
        dense(h, W2, b2, y, 16, 4, 0);
        cls = argmax(y, 4);
        print_int(cls);
        print_int(y[cls]);
    }
    return 0;
}
