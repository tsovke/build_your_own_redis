#include "hashtable.h"
#include <assert.h>
#include <stdlib.h>

// n must be a power of 2
static void h_init(HTab *htab, size_t n) {
  assert(n > 0 && ((n - 1) & n) == 0);
  htab->mask = n - 1;
  htab->size = 0;
}
