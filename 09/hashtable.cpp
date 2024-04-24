#include "hashtable.h"
#include <assert.h>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <endian.h>
#include <new>
#include <stdlib.h>

// n must be a power of 2
static void h_init(HTab *htab, size_t n) {
  assert(n > 0 && ((n - 1) & n) == 0);
  htab->tab = (HNode **)calloc(sizeof(HNode *), n);
  htab->mask = n - 1;
  htab->size = 0;
}

// hashtable insertion
static void h_insert(HTab *htab, HNode *node) {
  size_t pos = node->hcode & htab->mask;
  HNode *next = htab->tab[pos];
  node->next = next;
  htab->tab[pos] = node;
  htab->size++;
}

// hashtable look up subroutine.
// Pay attention to the return value. It returns the address of
// the parent pointer that owns the target node,
// which can be used to delete the target node.
static HNode **h_lookup(HTab *htab, HNode *key, bool (*eq)(HNode *, HNode *)) {
  if (!htab->tab) {
    return NULL;
  }

  size_t pos = key->hcode & htab->mask;
  HNode **from = &htab->tab[pos]; // incoming pointer to the result
  for (HNode *cur; (cur = *from) != NULL; from = &cur->next) {

    if (cur->hcode == key->hcode && eq(cur, key)) {
      return from;
    }
  }
  return NULL;
}

// remove a node from the chain
static HNode *h_detach(HTab *htab, HNode **from) {
  HNode *node = *from;
  *from = node->next;
  htab->size--;
  return node;
}

const size_t k_resizing_work = 128; // constant work

static void hm_help_resizing(HMap *hmap) {
  size_t nwork = 0;
  while (nwork < k_resizing_work && hmap->ht2.size > 0) {
    // scan for nodes from ht2 and move them to ht1
    HNode **from = &hmap->ht2.tab[hmap->resizing_pos];
    if (!*from) {
      hmap->resizing_pos++;
      continue;
    }

    h_insert(&hmap->ht1, h_detach(&hmap->ht2, from));
    nwork++;
  }

  if (hmap->ht2.size == 0 && hmap->ht2.tab) {
    // done
    free(hmap->ht2.tab);
    hmap->ht2 = HTab{};
  }
}
