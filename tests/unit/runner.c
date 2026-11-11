#include <stdio.h>

int lexer_test_run(void);
int parser_test_run(void);
int resolver_test_run(void);
int check_test_run(void);

int main(void)
{
    int failures;

    failures = 0;
    failures += lexer_test_run();
    failures += parser_test_run();
    failures += resolver_test_run();
    failures += check_test_run();

    if (failures != 0) {
        fprintf(stderr, "%d unit test failure(s)\n", failures);
        return 1;
    }

    printf("all unit tests passed\n");
    return 0;
}
