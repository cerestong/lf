#pragma once

#include "lf/limbo.hh"
#include "lf/slice.hh"

namespace lf
{

typedef char Row;
class NodeBase;
class LeafValue;
class KeyUnparsePrintableString;

enum
{
  LF_MAXKEYLEN = 512
};

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

  bool get(Slice &key, LeafValue &value, ThreadInfo *ti) const;

  template <typename H, typename F>
  int scan(H helper,
           Slice firstkey, bool emit_firstkey,
           F &scanner, ThreadInfo *ti) const;

  template <typename F>
  int scan(Slice firstkey, bool emit_firstkey,
           F &scanner,
           ThreadInfo *ti) const;

  template <typename F>
  int rscan(Slice firstkey, bool emit_firstkey,
            F &scanner,
            ThreadInfo *ti) const;

  template <typename P>
  void print(FILE *f = 0) const;

private:
  NodeBase *root_;
};
} // namespace lf
