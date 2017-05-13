/**
 *  this program test only MLFQ concurrency
 */

#include "types.h"
#include "stat.h"
#include "user.h"

void func(int input) {
    int i = 0;
    printf(1, "inside function argument's sp = %x\n", &input);
    printf(1, "inside function variable's sp = %x\n", &i);
}

int
main()
{
    int i = 0;
    int j = 0;
    int k = 0;
    printf(1, "i's sp = %x\n", &i);
    printf(1, "j's sp = %x\n", &j);
    printf(1, "k's sp = %x\n", &k);
    func(10);
    exit();
}
