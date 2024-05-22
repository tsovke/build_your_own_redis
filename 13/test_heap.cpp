#include "heap.cpp"
#include "heap.h"
#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <sys/types.h>
#include <utility>
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

static void add(Container &c, uint64_t val) {
  Data *d = new Data();
  c.map.insert(std::make_pair(val, d));
  HeapItem item;
  item.ref = &d->heap_idx;
  item.val = val;
  c.heap.push_back(item);
  heap_update(c.heap.data(), c.heap.size() - 1, c.heap.size());
}
