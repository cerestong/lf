#pragma once
#include <stdint.h>
#include <vector>
#include "lf/limbo.hh"
#include "lf/status.hh"

namespace lf
{

struct MCasHelper;
class MCasThreadCtx;

// 标识一个cas操作
struct CasRow
{
    intptr_t *address;
    intptr_t expected_value;
    intptr_t new_value;
    volatile MCasHelper*  mch; // 只从null 到非null值，之后就不会变
    
    CasRow() : address(nullptr), expected_value(0), new_value(0), mch(nullptr) {}
    CasRow(intptr_t *addr, intptr_t ev, intptr_t nv)
        : address(addr), expected_value(ev), new_value(nv), mch(nullptr) {}
};

extern Status init_wfmcas(size_t work_thread_no);

extern MCasThreadCtx *init_mcas_thread_ctx(size_t thd_id);

bool mcas(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
          std::vector<CasRow> &desc);

intptr_t mcas_read(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                   intptr_t *address);

} // end namespace
