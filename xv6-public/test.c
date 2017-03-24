#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
    printf(1, "My pid: %d\n", getpid());
    printf(1, "parent's pid: %d\n", getppid());
    exit();
}
