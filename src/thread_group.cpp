#include "thread_group.h"

#include <cstring>
#include <iostream>

#include "shared_data.h"

int ThreadGroup::Start(void *(*routine)(void *), void *arg, const char *name) {
  pthread_t thread{};
  const int ret = pthread_create(&thread, nullptr, routine, arg);
  if (ret != 0) {
    std::cerr << "Failed to create " << name << " thread: " << std::strerror(ret) << std::endl;
    return ret;
  }

  threads_.push_back(thread);
  return 0;
}

void ThreadGroup::WaitAll() {
  for (pthread_t thread : threads_)
    pthread_join(thread, nullptr);
  threads_.clear();
}

ThreadGroup::~ThreadGroup() {
  if (!threads_.empty()) {
    g_bExit = true;
    WaitAll();
  }
}
