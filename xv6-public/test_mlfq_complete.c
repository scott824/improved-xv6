/**
 *  this program test only MLFQ concurrency
 */

#include "types.h"
#include "stat.h"
#include "user.h"

#define NAME_CHILD_MLFQ     "test_mlfq"

char *child_argv[3][3] = {
    {NAME_CHILD_MLFQ, "0", 0},
    {NAME_CHILD_MLFQ, "0", 0},
    {NAME_CHILD_MLFQ, "0", 0}
};

int
main()
{
    int pid;
    int i;

    for(i = 0; i < 3; i++) {
        pid = fork();
        if (pid > 0) {
            continue;
        } else if (pid == 0) {
            exec(child_argv[i][0], child_argv[i]);
            printf(1, "exec failded!\n");
            exit();
        } else {
            printf(1, "fork failed!\n");
            exit();
        }
    }

    for (i = 0; i < 3; i++) {
        wait();
    }

    exit();
}
