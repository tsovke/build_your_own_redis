#pragma once

#include <cstdint>
#include <stddef.h>
#include <stdint.h>


// hashtable node, should be embedded into the payload
struct HNode{
  HNode *next=NULL;
  uint64_t hcode=0;
};

