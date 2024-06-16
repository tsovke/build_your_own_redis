#pragma once

#include <deque>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>

struct Work {
  void (*f)(void *) = NULL;
  void *arg = NULL;
};

struct TheadPool {
  std::vector<pthread_t> threads;
  std::deque<Work> queue;
  pthread_mutex_t mu;
  pthread_cond_t not_empty;
};
