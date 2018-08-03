#pragma once

#include "lf/limbo.hh"
#include "lf/slice.hh"

namespace lf
{

typedef char Row;
class NodeBase;
class LeafValue;
class KeyUnparsePrintableString;

struct NodeParams
{
  typedef KeyUnparsePrintableString key_unparse_type;
};

class BasicTable
{
  public:
    BasicTable();

    void initialize(ThreadInfo *ti);
    void destroy(ThreadInfo *ti);

    inline NodeBase *root() const;
    inline NodeBase *fix_root();

    bool get(Slice& key, LeafValue& value, ThreadInfo *ti) const;

    template <typename P>
    void print(FILE* f = 0) const;

  private:
    NodeBase *root_;
};
} // namespace lf
