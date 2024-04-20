#pragma once

#include <cstdint>
#include <stddef.h>
#include <stdint.h>


// hashtable, node, should be embeded into the payload
struct HNode {
  HNode *next=NULL;
  uint64_t hcode = 0;
};

// a simple fixed-sized hashtable
struct Htab {
  HNode **tab=NULL;
  size_t mask =0;
  size_t size = 0;
};

