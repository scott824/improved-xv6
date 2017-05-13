#include "types.h"
#include "stat.h"
#include "user.h"

void
*mythread(void *arg)
{
  printf(1, "***thread %d\n", (int*)arg);
  exit();
}

int
main()
{
  thread_t thread;
  printf(1, "test_thread\n");
  printf(1, "thread function's address: %x\n", mythread);
  thread_create(&thread, mythread, (void*)1);
  exit();
}

