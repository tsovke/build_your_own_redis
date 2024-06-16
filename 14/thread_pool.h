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


