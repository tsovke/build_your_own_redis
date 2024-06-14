#include "heap.h"
#include <stddef.h>
#include <stdint.h>

static size_t heap_parent(size_t i) { return (i + 1) / 2 - 1; }

static size_t heap_left(size_t i) { return i * 2 + 1; }

static size_t heap_right(size_t i) { return i * 2 + 2; }

static void heap_up(HeapItem *a, size_t pos) {
  HeapItem t = a[pos];
  while (pos > 0 && a[heap_parent(pos)].val > t.val) {
    // swap with the parent
    a[pos] = a[heap_parent(pos)];
    *a[pos].ref = pos;
    pos = heap_parent(pos);
  }
  a[pos] = t;
  *a[pos].ref = pos;
}
