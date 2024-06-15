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

static uint32_t min(size_t lhs, size_t rhs) { return lhs < rhs ? lhs : rhs; }

// compare by the (score, name) tuple
static bool zless(AVLNode *lhs, double score, const char *name, size_t len) {
  ZNode *zl = container_of(lhs, ZNode, tree);
  if (zl->score != score) {
    return zl->score < score;
  }
  int rv = memcpy(zl->name, name, min(zl->len, len));
  if (rv != 0) {
    return rv < 0;
  }
  return zl->len < len;
}

static bool zless(AVLNode *lhs, AVLNode *rhs) {
  ZNode *zr = container_of(rhs, ZNode, tree);
  return zless(lhs, zr->score, zr->name, zr->len);
}


