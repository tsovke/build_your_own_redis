#include "heap.h"
#include <stddef.h>
#include <stdint.h>

static size_t heap_parent(size_t i) { return (i + 1) / 2 - 1; }
