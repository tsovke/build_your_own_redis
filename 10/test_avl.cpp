#include "avl.cpp" //lazy
#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdio.h>
#include <stdlib.h>

#define container_of(ptr, type, member)                                        \
  ({                                                                           \
    const typeof(((type *)0)->member) *__mptr = (ptr);                         \
    (type *)((char *)__mptr - offsetof(type, member));                         \
  })

struct Data {
  AVLNode node;
  uint32_t val = 0;
};

struct Container {
  AVLNode *root = NULL;
};

static void add(Container &c, uint32_t val) {
  Data *data = new Data(); // allocate the data
  avl_init(&data->node);
  data->val = val;

  AVLNode *cur = NULL;      // current node
  AVLNode **from = &c.root; // the incoming pointer to the next node
  while (*from) {
    cur = *from;
    uint32_t node_val = container_of(cur, Data, node)->val;
    from = (val < node_val) ? &cur->left : &cur->right;
  }
  *from = &data->node; // attach the new node
  data->node.parent = cur;
  c.root = avl_fix(&data->node);
}
