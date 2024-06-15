#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdlib.h>
#include <string.h>
// proj
#include "avl.h"
#include "common.h"
#include "hashtable.h"
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
  int rv = memcmp(zl->name, name, min(zl->len, len));
  if (rv != 0) {
    return rv < 0;
  }
  return zl->len < len;
}

static bool zless(AVLNode *lhs, AVLNode *rhs) {
  ZNode *zr = container_of(rhs, ZNode, tree);
  return zless(lhs, zr->score, zr->name, zr->len);
}

// insert into the AVL tree
static void tree_add(ZSet *zset, ZNode *node) {
  AVLNode *cur = NULL;          // current node
  AVLNode **from = &zset->tree; // the incoming pointer to the next node
  while (*from) {
    cur = *from;
    from = zless(&node->tree, cur) ? &cur->left : &cur->right;
  }
  *from = &node->tree; // attach the new node
  node->tree.parent = cur;
  zset->tree = avl_fix(&node->tree);
}

// update the score of an existing node (AVL tree reinsertion)
static void zset_update(ZSet *zset, ZNode *node, double score) {
  if (node->score == score) {
    return;
  }
  zset->tree = avl_del(&node->tree);
  node->score = score;
  avl_init(&node->tree);
  tree_add(zset, node);
}

// add a new (score, name) tuple, or update the score of the existing tuple
bool zset_add(ZSet *zset, const char *name, size_t len, double score) {
  ZNode *node = zset_lookup(zset, name, len);
  if (node) {
    zset_update(zset, node, score);
    return false;
  } else {
    node = znode_new(name, len, score);
    hm_insert(&zset->hmap, &node->hmap);
    tree_add(zset, node);
    return true;
  }
}

// a helper structure for the hashtable lookup
struct HKey {
  HNode node;
  const char *name = NULL;
  size_t len = 0;
};

static bool hcmp(HNode *node, HNode *key) {
  ZNode *znode = container_of(node, ZNode, hmap);
  HKey *hkey = container_of(key, HKey, node);
  if (znode->len != hkey->len) {
    return false;
  }
  return 0 == memcmp(znode->name, hkey->name, znode->len);
}
