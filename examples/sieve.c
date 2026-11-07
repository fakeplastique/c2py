int composite[100];

int main() {
    int i, j, count = 0;
    for (i = 2; i < 100; i++) {
        if (composite[i])
            continue;
        count++;
        printf("%d ", i);
        for (j = i * i; j < 100; j += i)
            composite[j] = 1;
    }
    printf("\ncount = %d\n", count);
    return count;
}
