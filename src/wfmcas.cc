#include "lf/wfmcas.hh"
#include "lf/logger.hh"
#include "lf/compiler.hh"
#include "lf/random.hh"

#include <algorithm>
#include <thread>

namespace lf
{

enum
{
    maxFail = 4, // 最多尝试maxFail次，之后making an announcement
};

const intptr_t mch_fail_flag = (intptr_t)-1;
intptr_t *end_of_casrow = (intptr_t *)1;

struct MCasHelper
{
    CasRow *cr; // 只有在cr->mch == this, MCasHelper与CasRow关联才有效
};

// 每个工作线程持有一个threadCtx，相当于__thread 变量
struct MCasThreadCtx
{
    size_t thread_id; // thread-local 线程ID
    size_t check_id;  // thread-local 用于线程检查的id
    int recur_depth;  // thread-local 用于标识递归深度（帮助其他线程完成operations）
};

void place_mcas_helper(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                       CasRow *cr, CasRow *last_row);

bool should_replace(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                    intptr_t ev, MCasHelper *mch);
void help_if_needed(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl);
int help_complete(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                  CasRow *cr);
void remove_mcas_helper(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                        bool passed, CasRow *m, CasRow *lastRow);

// 全局数组，每个线程一个位置用于 write announcement
size_t gMCasThreadNo = 0;
std::vector<CasRow *> gPendingOpTable;
std::vector<MCasThreadCtx> gMCasThreadCtxs;

Status init_wfmcas(size_t work_thread_no)
{
    gPendingOpTable.resize(work_thread_no);
    gMCasThreadCtxs.resize(work_thread_no);
    gMCasThreadNo = work_thread_no;

    return Status::OK();
}

MCasThreadCtx *init_mcas_thread_ctx(size_t thd_id)
{
    assert(thd_id < gMCasThreadCtxs.size());
    MCasThreadCtx *ctx = &(gMCasThreadCtxs[thd_id]);
    ctx->thread_id = thd_id;
    ctx->check_id = (thd_id + 1) % gMCasThreadNo;
    ctx->recur_depth = 0;
    return ctx;
}

inline bool is_mcas_helper(intptr_t val)
{
    return ((uintptr_t)val >> 63);
}
inline intptr_t mcas_helper_mask(intptr_t val)
{
    return (intptr_t)((uintptr_t)val | ((uintptr_t)1 << 63));
}
inline intptr_t mcas_helper_unmask(intptr_t val)
{
    return (intptr_t)((uintptr_t)val & (((uintptr_t)1 << 63) - 1));
}

static MCasHelper *allocate_mcas_helper(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                                        CasRow *cr)
{
    MCasHelper *mch = (MCasHelper *)limbo_hdl->alloc(sizeof(MCasHelper));
    mch->cr = cr;
    assert(mch->cr != nullptr);
    return mch;
}

// 返回MCAS操作是否成功
// CasRow的任意长的数组，以0x1标识结束
bool invoke_mcas(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                 CasRow *mcasp, CasRow *last_row)
{
    CasRow *ori_mcasp = mcasp;
    bool res = false;
    thd_ctx->recur_depth = 0;
    // 开始操作之前先查看是否需要帮助其他线程，帮助一直被抢占的线程完成操作
    help_if_needed(thd_ctx, limbo_hdl);

    // 调用placeMCasHelper,直到持有所有的addresses，或失败。
    // 中间线程可能被系统多次调度
    do
    {
        // lastRow->mch 标识MCAS操作状态：null 还在进行、~0x0 失败、成功
        // 此方法可以削减状态管理的cas操作
        if (last_row->mch == nullptr)
        {
            place_mcas_helper(thd_ctx, limbo_hdl, mcasp, last_row);
        }
        else
        {
            break;
        }
    } while ((mcasp++) != last_row);

    atomic_storeptr_relaxed((void **)&(gPendingOpTable[thd_ctx->thread_id]), nullptr);
    // lastRow->mch == ~0x0, 标识操作失败
    res = ((intptr_t)(last_row->mch) != mch_fail_flag);
    // 将MCasHelper替换为真实值
    remove_mcas_helper(thd_ctx, limbo_hdl, res, ori_mcasp, last_row);

    return res;
}

inline void set_mcas_fail(CasRow *cr, CasRow *last_row)
{
    void *pnull = nullptr;
    if (atomic_casptr((void **)(&cr->mch), (void **)(&pnull), (void *)mch_fail_flag))
    {
        atomic_casptr((void **)(&last_row->mch), (void **)(&pnull), (void *)mch_fail_flag);
    }
}
// 如果address的逻辑值等于expectedValue,则设置address值为MCasHelper地址
// 先为address设置MCasHelper, 再为CasRow关联MCasHelper
// 如果address的逻辑值不等于expectedValue,则在返回前设置cr->mch和lastRow->mch为~0x0
// CasRow和MCasHelper的关联，若关联失败，则意味着cr已经关联过了。其他线程已经完成了这个
// MCAS操作，线程的mch应该撤销。
void place_mcas_helper(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                       CasRow *cr, CasRow *last_row)
{
    intptr_t *address = cr->address;
    intptr_t evalue = cr->expected_value;
    MCasHelper *mch = nullptr;
    mch = allocate_mcas_helper(thd_ctx, limbo_hdl, cr);
    intptr_t cvalue = atomic_load_relaxed(address);
    int tries = 0;
    while (cr->mch == nullptr)
    {
        if (unlikely(tries++ == maxFail))
        {
            if (thd_ctx->recur_depth > 0)
            {
                set_mcas_fail(cr, last_row);
                if (likely(cr->mch != mch))
                {
                    limbo_hdl->dealloc(mch);
                }
                return;
            }

            // 在尝试maxFail次后，线程将自己的MCAS写到全局变量pendingOpTable[threadID]里
            // 其他线程保证最总会看到此操作，并会尝试帮助完成此操作。
            // CasRow和MCasHelper的关联，保证不会有多个线程去尝试完成同一个操作
            atomic_storeptr_relaxed((void **)&(gPendingOpTable[thd_ctx->thread_id]), cr);
        }
        if (!is_mcas_helper(cvalue))
        {
            // address里是值
            // 设置address为MCasHelper
            intptr_t ev = evalue;
            if (atomic_cas64(address, &ev, mcas_helper_mask((intptr_t)mch)))
            {
                // address值等于excepectValue,尝试设置cr->mch为mch
                MCasHelper *emch = nullptr;
                if ((!atomic_casptr((void **)(&cr->mch), (void **)(&emch), mch)) && (emch != mch))
                {
                    // cr->mch的值指向其他mch, 意味着：其他线程完成了此MCas操作,撤销本线程的操作
                    // 这个CAS是否正确？因为mch是此线程独有的，
                    // 所以在cValue = CAS(address, eValue, mch)执行成功后，
                    // 不会有其他线程对address修改
                    ev = mcas_helper_mask((intptr_t)mch);
                    if (atomic_cas64(address, &ev, evalue))
                    {
                        limbo_hdl->dealloc(mch);
                    }
                }
                else
                {
                }
                return;
            }
            else if (is_mcas_helper(ev))
            {
                // CAS操作失败时，这是不能断定MCAS操作失败。
                // 因为此时address里可能存储的是MCasHelper，因此值还未确定
                // 这里存在一个无限循环？？？， 每次循环都address里都是value，并且value不等于eValue.
                cvalue = ev;
                continue;
            }
            else
            {
                // address的值与eValue不相同
                // 这是我自己加的？？？
            }
        }
        else
        {
            acquire_fence();
            MCasHelper *cmch = (MCasHelper *)mcas_helper_unmask(cvalue);
            // address引用了MCasHelper,并且指向cr
            if (cr == cmch->cr)
            {
                MCasHelper *emch = nullptr;
                // 尝试设置cr->mch 为引用的MCasHelper
                if ((!atomic_casptr((void **)(&cr->mch), (void **)(&emch), cmch)) && (emch != cmch))
                {
                    // ？？？，cr->mch的值指向其他mch, 意味着：其他线程完成了此MCas操作,撤销本线程的操作
                    // cr == cValue->cr 意味着在设置address时，address上的值是eValue（cr->expectedValue是常量）.
                    // 而address的值一旦设置以后，其他线程是不可能给它设置除eValue之外的值。
                    // 所以这看起来是合理的。
                    // CasRow->mch一旦设置后，就不会变。这意味着CasRow->mch一旦设置后，此address的cas操作已经完成
                    intptr_t cvalue1 = cvalue;
                    if (atomic_cas64(address, &cvalue1, evalue))
                    {
                        log("%p dealloc %p from 2.1.1", thd_ctx, cmch);
                        limbo_hdl->dealloc(cmch);
                    }
                    log("%p path2.1.1 mch %p, emch %p", thd_ctx, mch, emch);
                }
                else
                {
                }
                if (likely(mch != emch))
                {
                    limbo_hdl->dealloc(mch);
                }
                return;
            }
            else if (should_replace(thd_ctx, limbo_hdl, evalue, cmch))
            {
                // 设置address为MCasHelper
                intptr_t ev = cvalue;
                if (atomic_cas64(address, &ev, mcas_helper_mask((intptr_t)mch)))
                {
                    MCasHelper *emch = nullptr;
                    if ((!atomic_casptr((void **)(&cr->mch), (void **)(&emch), mch)) && (emch != mch))
                    {
                        // cr->mch的值指向其他mch, 意味着：其他线程完成了此MCas操作,撤销本线程的操作
                        // 这个CAS是否正确？因为mch是此线程独有的，
                        // 所以在cValue = CAS(address, eValue, mch)执行成功后，
                        // 不会有其他线程对address修改
                        ev = mcas_helper_mask((intptr_t)mch);
                        if (atomic_cas64(address, &ev, evalue))
                        {
                            log("%p dealloc %p from 2.2.1", thd_ctx, mch);
                            limbo_hdl->dealloc(mch);
                        }
                        log("%p path2.2.1.1 mch %p, emch %ld", thd_ctx, mch, emch);
                    }
                    else
                    {
                    }
                    return;
                }
                else
                {
                    cvalue = ev;
                    continue;
                }
            }
            else
            {
                // 数据不同，MCAS操作失败
            }
        }
        // cr->mch == null 标识此操作失败
        // 设置cr->mch 和 lastRow->mch 为~0x0
        set_mcas_fail(cr, last_row);
        break;
    }

    if (likely(cr->mch != mch))
    {
        limbo_hdl->dealloc(mch);
    }
    return;
}

// 判断mch的逻辑值（新值或旧址）是否与ev值相等
// 注意这里并没有真正修改address的值，只是找出一个值进行比较。
// 真正的操作都是通过CAS操作完成的。
bool should_replace(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                    intptr_t ev, MCasHelper *mch)
{
    //memory_fence();
    CasRow *cr = (CasRow *)(mch->cr);
    assert(cr != nullptr);

    // 检查mch引用的cr的expectedValue和newValue值是否与ev匹配
    if ((cr->expected_value != ev) && (cr->new_value != ev))
    {
        return false;
    }
    else
    {
        // 调用helpComplete,确保引用到的MCAS处于完成状态。
        int res = help_complete(thd_ctx, limbo_hdl, cr);
        if ((res == 0) && ((MCasHelper *)atomic_loadptr_relaxed((void **)&(cr->mch)) == mch))
        {
            return (cr->new_value == ev);
        }
        else if (res == -1)
        {
            return false;
        }
        else
        {
            // 1. res==false: cr标识的MCAS无效，使用ev
            // 2. res == true && cr->mch != mch , 意味着mch标识的cr已经由其他线程完成。
            //    标识mch的线程会用ev回复address的值
            return (cr->expected_value == ev);
        }
    }
}

void help_if_needed(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl)
{
    thd_ctx->check_id = (thd_ctx->check_id + 1) % (gMCasThreadNo);
    if (thd_ctx->check_id == thd_ctx->thread_id)
        return;
    CasRow *cr = (CasRow *)atomic_loadptr_relaxed((void **)&(gPendingOpTable[thd_ctx->check_id]));
    if (cr != nullptr)
    {
        acquire_fence();
        help_complete(thd_ctx, limbo_hdl, cr);
    }
}

// ret: -1 full return, 0 sucess, 1  false
int help_complete(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                  CasRow *cr)
{
    thd_ctx->recur_depth++;
    if (thd_ctx->recur_depth >= gMCasThreadNo)
    {
        /*
		递归调用helpComplete的深度大于nThreads，意味着：
		线程自己的MCAS操作的依赖关系已经变化了。
		线程应该回去处理自己的操作，增加failCount,尝试重新获取address
		*/
        return -1;
    }
    // 获取lastRow位置,通过lastRow获知操作是否已经结束。
    CasRow *last_row = cr + 1;
    while (last_row->address != end_of_casrow)
    {
        last_row++;
    }
    last_row--;

    do
    {
        if (last_row->mch == nullptr)
        {
            place_mcas_helper(thd_ctx, limbo_hdl, cr, last_row);
            if ((intptr_t)atomic_loadptr_relaxed((void **)&(cr->mch)) == mch_fail_flag)
            {
                break;
            }
        }
        else
        {
            break;
        }
    } while ((cr++) != last_row);
    thd_ctx->recur_depth--;
    return ((intptr_t)atomic_loadptr_relaxed((void **)&(last_row->mch)) != mch_fail_flag) ? 0 : 1;
}

void remove_mcas_helper(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                        bool passed, CasRow *m, CasRow *last_row)
{
    assert(m <= last_row);
    do
    {
        MCasHelper *mch = (MCasHelper *)atomic_loadptr_relaxed((void **)&(m->mch));
        intptr_t ev = mcas_helper_mask((intptr_t)(mch));
        if ((intptr_t)mch == mch_fail_flag)
        {
            return;
        }
        else if (passed)
        {
            atomic_cas64(m->address, &ev, m->new_value);
        }
        else
        {
            atomic_cas64(m->address, &ev, m->expected_value);
        }
        limbo_hdl->dealloc((void *)(mch));
    } while ((m++) != last_row);
}

bool sort_by_address_desc(const CasRow &a, const CasRow &b)
{
    return (intptr_t)(a.address) > (intptr_t)(b.address);
}

bool mcas(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
          std::vector<CasRow> &desc)
{
    if (desc.empty())
    {
        return true;
    }
    std::sort(desc.begin(), desc.end(), sort_by_address_desc);

    CasRow *mcasp = (CasRow *)limbo_hdl->alloc(sizeof(CasRow) * (desc.size() + 1));
    for (size_t i = 0; i < desc.size(); i++)
    {
        new (mcasp + i) CasRow(desc[i].address, desc[i].expected_value, desc[i].new_value);
        assert(mcasp[i].address);
    }
    new (mcasp + desc.size()) CasRow(end_of_casrow, 0, 0);
    CasRow *last_row = mcasp + desc.size() - 1;

    bool ret = invoke_mcas(thd_ctx, limbo_hdl, mcasp, last_row);

    limbo_hdl->dealloc(mcasp);
    return ret;
}

intptr_t mcas_read(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                   intptr_t *address)
{
    assert(thd_ctx->recur_depth == 0);
    intptr_t cvalue = atomic_load_relaxed(address);
    if (!is_mcas_helper(cvalue))
    {
        return cvalue;
    }
    else
    {
        acquire_fence();
        MCasHelper *mch = (MCasHelper *)mcas_helper_unmask(cvalue);
        CasRow *cr = mch->cr;
        int res = help_complete(thd_ctx, limbo_hdl, cr);
        if ((res == 0) && ((MCasHelper *)atomic_loadptr_relaxed((void **)&(cr->mch)) == mch))
        {
            return mch->cr->new_value;
        }
        else
        {
            return mch->cr->expected_value;
        }
    }
}

} // end namespace
