extern void print_int(int);
int counter;
int limit = 25;
int hist[5];
int next() { counter += 3; return counter; }
void record(int v) { hist[v % 5]++; }
int main() {
    int i;
    while (next() < limit) { record(counter); }
    print_int(counter);
    for (i = 0; i < 5; i++) print_int(hist[i]);
    limit -= 5; limit *= 2; limit |= 1; limit ^= 8; limit <<= 2; limit >>= 1; limit %= 1000; limit /= 3;
    print_int(limit);
    counter = counter * 2 + limit;
    print_int(counter);
    return 0;
}
