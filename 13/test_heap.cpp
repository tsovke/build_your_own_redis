#include "heap.cpp"
#include "heap.h"
#include <assert.h>
#include <cstddef>
#include <map>
#include <sys/types.h>
#include <vector>

struct Data {
  size_t heap_idx = -1;
};

struct Container {
  std::vector<HeapItem> heap;
  std::multimap<u_int64_t, Data *> map;
};

static void dispose(Container &c) {
  for (auto p : c.map) {
    delete p.second;
  }
}
