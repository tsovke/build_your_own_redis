#pragma once

#include <cstdint>
#include <stddef.h>
#include <stdint.h>

// hashtable, node, should be embeded into the payload
struct HNode {
  HNode *next = NULL;
  uint64_t hcode = 0;
};

// a simple fixed-sized hashtable
struct HTab {
  HNode **tab = NULL;
  size_t mask = 0;
  size_t size = 0;
};

// the real hashtable interface.
// it uses 2 hashtables for progressive resizing.
struct HMap {
  HTab ht1; // newer
  HTab ht2; // older
  size_t resizing_pos = 0;
};
