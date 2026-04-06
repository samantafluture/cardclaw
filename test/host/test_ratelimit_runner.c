/*
 * Dedicated runner for real ratelimit.c runtime tests.
 */

#include <stdio.h>

extern int test_ratelimit_all(void);

int main(void)
{
    int failures;

    printf("zclaw Ratelimit Runtime Tests\n");
    printf("=============================\n\n");

    failures = test_ratelimit_all();

    printf("\n=============================\n");
    if (failures == 0) {
        printf("All tests passed!\n");
        return 0;
    }

    printf("%d test(s) failed\n", failures);
    return 1;
}
