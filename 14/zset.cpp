#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <stdlib.h>
#include <string.h>
// proj
#include "avl.h"
#include "common.h"
#include "zset.h"

static ZNode *znode_new(const char *name, size_t len, double score) {
  ZNode *node = (ZNode *)malloc(sizeof(ZNode) + len);
  assert(node); // not a good idea in real projects
  avl_init(&node->tree);
  node->hmap.next = NULL;
  node->hmap.hcode = str_hash((uint8_t *)name, len);
  node->score = score;
  node->len = len;
  memcpy(&node->name[0], name, len);
  return node;
}
