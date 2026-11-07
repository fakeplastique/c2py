int is_odd(int n);

int is_even(int n) {
    if (n == 0) return 1;
    return is_odd(n - 1);
}

int is_odd(int n) {
    if (n == 0) return 0;
    return is_even(n - 1);
}

int main() {
    int i = 0;
    do {
        i++;
        if (i % 3 == 0) continue;
        printf("%d:%d ", i, is_even(i));
    } while (i < 10);
    printf("\n");
    return 0;
}
