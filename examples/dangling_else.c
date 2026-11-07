int classify(int a, int b) {
    if (a > 0)
        if (b > 0)
            return 1;
        else
            return 2;
    return 3;
}

int main() {
    printf("%d %d %d\n", classify(1, 1), classify(1, -1), classify(-1, 1));
    return 0;
}
