#define _GNU_SOURCE
#include <sys/prctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

//#ifndef PR_SET_THP_DISABLE
//#define PR_SET_THP_DISABLE 41
//#define PR_GET_THP_DISABLE 42
//#endif

int main(int argc, char *argv[]) {
    if (prctl(PR_SET_THP_DISABLE, 1, 0, 0, 0) != 0) {
        perror("prctl(PR_SET_THP_DISABLE)");
        return 1;
    }

    // Run the desired command
    if (argc > 1) {
        execvp(argv[1], &argv[1]);
        perror("execvp");
        return 1;
    }

    fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
    return 1;
}

