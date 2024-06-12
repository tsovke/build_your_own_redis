#include "hashtable.h"
#include <assert.h>
#include <cstddef>
#include <stdlib.h>

// n must be a power of 2
static void h_init(HTab *htab, size_t n) {
  assert(n > 0 && ((n - 1) & n) == 0);
  htab->mask = n - 1;
  htab->size = 0;
}

// hashtable insertion
static void h_insert(HTab *htab,HNode *node ){
  size_t pos=node->hcode&htab->mask;
  HNode *next=htab->tab[pos];
  node->next=next;htab->tab[pos]=node;
  htab->size++;
}

// hashtable look up subroutine.
// Pay attention to the return value. It returns the address of
// the parent pointer that owns the target node,
// which can be used to delete the target node.
static HNode **h_lookup(){
  
}
