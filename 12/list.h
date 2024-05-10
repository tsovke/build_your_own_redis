#pragma once

#include <stddef.h>

struct DList {
  DList *prev = NULL;

  DList *next = NULL;
};

inline void dlist_init(DList *node) { node->prev = node->next = node; }

inline bool dlist_empty(DList *node) { return node->next == node; }


