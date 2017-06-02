#include "types.h"
#include "stat.h"
#include "user.h"

struct test {
  int a;
  int b;
};

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
  printf(1, "hello thread\n");
  printf(1, "start thread sp=%x\n", arg);
  struct test *ret = (struct test*)arg;
  sleep(100);
  printf(1, "***thread %d\n", ret->a);

  //int *testmalloc;
  //testmalloc = malloc(sizeof(int));
  //printf(1, "%d malloc address %d\n", getpid(), testmalloc);
  //thread_t thread;

  //thread_create(&thread, mythread2, &argu);
  //thread_join(thread, &retval);

  thread_exit((void*)ret);
}

int
main()
{
  struct test t;
  t.a = 1234;
  t.b = 5678;
  void *retval;
  thread_t thread1, thread2, thread3;
  printf(1, "test_thread\n");
  printf(1, "function pointer %d\n", mythread);

  thread_create(&thread1, mythread, &t);
  //thread_create(&thread2, mythread, &t);
  //thread_create(&thread3, mythread, &t);
  printf(1, "join\n");
  thread_join(thread1, &retval);
  //thread_join(thread2, &retval);
  //thread_join(thread3, &retval);

  printf(1, "this is main\n");
  exit();
  //thread_exit(0);
}

