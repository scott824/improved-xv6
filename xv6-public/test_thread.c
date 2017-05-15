#include "types.h"
#include "stat.h"
#include "user.h"

void*
mythread2(void *arg)
{
  int *ret = (int*)arg;
  printf(1, "******thread %d\n", *ret);
  thread_exit((void*)ret);
}

void
*mythread(void *arg)
{
  int *ret = (int*)arg;
  printf(1, "***thread %d\n", *ret);

  thread_t thread;
  int argu = 5678;
  void *retval;

  thread_create(&thread, mythread2, &argu);
  thread_join(thread, &retval);

  thread_exit((void*)ret);
}

int
main()
{
  int argu = 1234;
  void *retval;
  thread_t thread;
  printf(1, "test_thread\n");
  printf(1, "thread function's address: %x\n", mythread);
  thread_create(&thread, mythread, &argu);
  printf(1, "thread number: %d\n", thread);
  thread_join(thread, &retval);
  printf(1, "this is main %d\n", *((int*)retval));
  exit();
}

