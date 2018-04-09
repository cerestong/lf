#pragma once
#include "lf/alloc_pin.hh"

namespace lf
{
namespace sortlist
{
struct Item
{
  intptr_t volatile link; // a pointer to the next element in a list and a flag
  uint64_t key;
  const char *key;
  size_t keylen;
};

struct Cursor
{
  intptr_t volatile *prev;
  Item *curr, *next;
};

inline Item *ptr(intptr_t v) { return (Item *)(v & (~(intptr_t)1)); }
inline bool deleted(intptr_t v) { return (v & 1); }

int lfind(Item *volatile *head, uint64_t key, Cursor *cursor, Pins *pins);
Item *linsert(Item *volatile *head, Item *node, Pins *pins);
int ldelete(Item *volatile *head, uint64_t key, Pins *pins);

} // end namespace
} // end namespace sortlist
