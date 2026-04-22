#ifndef THREAD_GROUP_H
#define THREAD_GROUP_H

#include <pthread.h>

#include <vector>

class ThreadGroup {
public:
  int Start(void *(*routine)(void *), void *arg, const char *name);
  void WaitAll();
  ~ThreadGroup();

private:
  std::vector<pthread_t> threads_;
};

#endif  // THREAD_GROUP_H
