#include "masstree/mt_key.hh"

namespace lf  {

    int StringSlice::size = (int)sizeof(uint64_t);

    int MtKey::ikey_size = StringSlice::size;
}

