#include "lf/wfmcas.hh"
#include "lf/logger.hh"

#include <setjmp.h>
#include <algorithm>

namespace lf
{

enum
{
    maxFail = 8, // 最多尝试maxFail次，之后making an announcement
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
    size_t thread_id;  // thread-local 线程ID
    size_t check_id;   // thread-local 用于线程检查的id
    int recur_depth;   // thread-local 用于标识递归深度（帮助其他线程完成operations）
    MCasHelper *tl_op; // thread-local 用于标识线程自己的operation
    jmp_buf env;       // 标记full return
    std::vector<MCasHelper *> mchs_buf;
    size_t mchs_init_length;
};

void place_mcas_helper(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                       CasRow *cr, CasRow *last_row, bool firstTime);

bool should_replace(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                    intptr_t ev, MCasHelper *mch);
void help_if_needed(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl);
bool help_complete(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                   CasRow *cr);
void remove_mcas_helper(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                        bool passed, CasRow *m, CasRow *lastRow);

// 全局数组，每个线程一个位置用于 write announcement
size_t gMCasThreadNo = 0;
std::vector<MCasHelper *> gPendingOpTable;
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
    ctx->tl_op = nullptr;
    ctx->mchs_buf.reserve(gMCasThreadNo);
    return ctx;
}

inline bool is_mcas_helper(intptr_t val)
{
    return (uintptr_t)val & ((uintptr_t)1 << 63);
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
    //log("%p alloc mcas %p", thd_ctx, mch);
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
    thd_ctx->mchs_buf.resize(0);
    // 开始操作之前先查看是否需要帮助其他线程，帮助一直被抢占的线程完成操作
    help_if_needed(thd_ctx, limbo_hdl);
    place_mcas_helper(thd_ctx, limbo_hdl, mcasp++, last_row, true);

    // 调用placeMCasHelper,直到持有所有的addresses，或失败。
    // 中间线程可能被系统多次调度
    do
    {
        // lastRow->mch 标识MCAS操作状态：null 还在进行、~0x0 失败、成功
        // 此方法可以削减状态管理的cas操作
        if (last_row->mch == nullptr)
        {
            place_mcas_helper(thd_ctx, limbo_hdl, mcasp, last_row, false);
        }
        else
        {
            break;
        }
    } while ((mcasp++) != last_row);

    gPendingOpTable[thd_ctx->thread_id] = nullptr;
    // lastRow->mch == ~0x0, 标识操作失败
    res = ((intptr_t)(last_row->mch) != mch_fail_flag);
    // 将MCasHelper替换为真实值
    remove_mcas_helper(thd_ctx, limbo_hdl, res, ori_mcasp, last_row);
    assert(thd_ctx->mchs_buf.empty());

    return res;
}

// 如果address的逻辑值等于expectedValue,则设置address值为MCasHelper地址
// 先为address设置MCasHelper, 再为CasRow关联MCasHelper
// 如果address的逻辑值不等于expectedValue,则在返回前设置cr->mch和lastRow->mch为~0x0
// CasRow和MCasHelper的关联，若关联失败，则意味着cr已经关联过了。其他线程已经完成了这个
// MCAS操作，线程的mch应该撤销。
void place_mcas_helper(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                       CasRow *cr, CasRow *last_row, bool firstTime)
{
    //log("%p place E, %d, cr %p", thd_ctx, thd_ctx->recur_depth, cr);
    intptr_t *address = cr->address;
    intptr_t evalue = cr->expected_value;
    MCasHelper *mch = nullptr;
    mch = allocate_mcas_helper(thd_ctx, limbo_hdl, cr);
    thd_ctx->mchs_buf.push_back(mch);
    intptr_t cvalue = *address;
    int tries = 0;
    while (firstTime || cr->mch == nullptr)
    {
        if (tries++ == maxFail)
        {
            if (firstTime)
            {
                thd_ctx->tl_op = mch;
                firstTime = false;
            }
            // 在尝试maxFail次后，线程将自己的MCAS写到全局变量pendingOpTable[threadID]里
            // 其他线程保证最总会看到此操作，并会尝试帮助完成此操作。
            // CasRow和MCasHelper的关联，保证不会有多个线程去尝试完成同一个操作
            gPendingOpTable[thd_ctx->thread_id] = thd_ctx->tl_op;
            if (thd_ctx->recur_depth > 0)
            {
                // Full return指的是返回直到开始执行自己的操作
                //log("%p longjmp from place_mcas_helper, recur_depth %d", thd_ctx, thd_ctx->recur_depth);
                longjmp(thd_ctx->env, 1);
            }
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
                        //log("%p direct dealloc %p from 1", thd_ctx, mch);
                        limbo_hdl->dealloc(mch);
                    }
                }
                thd_ctx->mchs_buf.pop_back();
                //log("%p data return 1", thd_ctx);

                return;
            }
            else if (is_mcas_helper(ev))
            {
                // CAS操作失败时，这是不能断定MCAS操作失败。
                // 因为此时address里可能存储的是MCasHelper，因此值还未确定
                // 这里存在一个无限循环？？？， 每次循环都address里都是value，并且value不等于eValue.
                cvalue = ev;
                //log("%p data return 2", thd_ctx);
                continue;
            }
            else
            {
                // address的值与eValue不相同
                // 这是我自己加的？？？
                //log("%p data return 3", thd_ctx);
            }
        }
        else
        {
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
                        //log("%p direct dealloc %p from 2", thd_ctx, cmch);
                        limbo_hdl->dealloc(cmch);
                    }
                }
                if (mch != emch)
                {
                    //log("%p direct dealloc %p from 3", thd_ctx, mch);
                    thd_ctx->mchs_buf.pop_back();
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
                            //log("%p direct dealloc %p from 4", thd_ctx, mch);
                            limbo_hdl->dealloc(mch);
                        }
                    }
                    thd_ctx->mchs_buf.pop_back();
                    //log("%p should_replace return 1", thd_ctx);
                    return;
                }
                else
                {
                    cvalue = ev;
                    //log("%p should_replace contine 2", thd_ctx);
                    continue;
                }
            }
            else
            {
                // 数据不同，MCAS操作失败
                //log("%p should_replace return 3", thd_ctx);
            }
        }
        // cr->mch == null 标识此操作失败
        // 设置cr->mch 和 lastRow->mch 为~0x0
        void *pnull = nullptr;
        if (atomic_casptr((void **)(&cr->mch), (void **)(&pnull), (void *)mch_fail_flag))
        {
            atomic_casptr((void **)(&last_row->mch), (void **)(&pnull), (void *)mch_fail_flag);
        }
        //log("%p direct dealloc %p from 5", thd_ctx, mch);
        thd_ctx->mchs_buf.pop_back();
        limbo_hdl->dealloc(mch);

        return;
    }

    //log("%p direct dealloc %p from 6", thd_ctx, mch);
    thd_ctx->mchs_buf.pop_back();
    limbo_hdl->dealloc(mch);
    //log("%p place X, %d, cr %p", thd_ctx, thd_ctx->recur_depth, cr);
    return;
}

// 判断mch的逻辑值（新值或旧址）是否与ev值相等
// 注意这里并没有真正修改address的值，只是找出一个值进行比较。
// 真正的操作都是通过CAS操作完成的。
bool should_replace(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                    intptr_t ev, MCasHelper *mch)
{
    CasRow *cr = mch->cr;
    // 检查mch引用的cr的expectedValue和newValue值是否与ev匹配
    if ((cr->expected_value != ev) && (cr->new_value != ev))
    {
        return false;
    }
    else
    {
        // 调用helpComplete,确保引用到的MCAS处于完成状态。
        bool res = help_complete(thd_ctx, limbo_hdl, cr);
        if (res && (cr->mch == mch))
        {
            if (cr->new_value == ev)
            {
                return true;
            }
            else
            {
                return false;
            }
        }
        else
        {
            // 1. res==false: cr标识的MCAS无效，使用ev
            // 2. res == true && cr->mch != mch , 意味着mch标识的cr已经由其他线程完成。
            //    标识mch的线程会用ev回复address的值
            if (cr->expected_value == ev)
            {
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

void help_if_needed(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl)
{
    thd_ctx->check_id = (thd_ctx->check_id + 1) % (gMCasThreadNo);
    if (thd_ctx->check_id == thd_ctx->thread_id)
        return;
    MCasHelper *mch = gPendingOpTable[thd_ctx->check_id];
    if (mch != nullptr)
    {
        //log("%p help_if_needed E", thd_ctx);

        help_complete(thd_ctx, limbo_hdl, mch->cr);

        //log("%p help_if_needed X", thd_ctx);
    }
}

bool help_complete(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                   CasRow *cr)
{
    if (thd_ctx->recur_depth == 0)
    {
        thd_ctx->mchs_init_length = thd_ctx->mchs_buf.size();
        if (thd_ctx->mchs_init_length != 1)
        {
            //log("%p mchs_init_length %d", thd_ctx, thd_ctx->mchs_init_length);
        }
        if (setjmp(thd_ctx->env) != 0)
        {
            assert(false);
            // full return
            thd_ctx->recur_depth = 0;

            //log("%p FULL RETURN mchs_buf.size: %d", thd_ctx, thd_ctx->mchs_buf.size());
            for (size_t i = thd_ctx->mchs_init_length; i < thd_ctx->mchs_buf.size(); i++)
            {
                //log("%p FULL RETURN direct dealloc %p", thd_ctx, thd_ctx->mchs_buf[i]);
                limbo_hdl->dealloc(thd_ctx->mchs_buf[i]);
            }
            assert(thd_ctx->mchs_buf.size() > 1);
            thd_ctx->mchs_buf.resize(thd_ctx->mchs_init_length);
            return false;
        }
    }
    thd_ctx->recur_depth++;
    if (thd_ctx->recur_depth > gMCasThreadNo)
    {
        /*
		递归调用helpComplete的深度大于nThreads，意味着：
		线程自己的MCAS操作的依赖关系已经变化了。
		线程应该回去处理自己的操作，增加failCount,尝试重新获取address
		*/
        //log("%p longjmp from help_complete, recur_depth %d", thd_ctx, thd_ctx->recur_depth);
        longjmp(thd_ctx->env, 1);
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
            place_mcas_helper(thd_ctx, limbo_hdl, cr, last_row, false);
            if ((intptr_t)cr->mch == mch_fail_flag)
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
    return ((intptr_t)last_row->mch != mch_fail_flag);
}

void remove_mcas_helper(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                        bool passed, CasRow *m, CasRow *last_row)
{
    assert(m <= last_row);
    do
    {
        if ((intptr_t)(m->mch) == mch_fail_flag)
        {
            return;
        }
        else if (passed)
        {
            intptr_t ev = mcas_helper_mask((intptr_t)(m->mch));
            atomic_cas64(m->address, &ev, m->new_value);
        }
        else
        {
            intptr_t ev = mcas_helper_mask((intptr_t)(m->mch));
            atomic_cas64(m->address, &ev, m->expected_value);
        }
        //log("%p passed %d remove_mcas_helper dealloc %p", thd_ctx, passed, (void *)m->mch);
        limbo_hdl->dealloc((void *)(m->mch));
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
    CasRow tail;
    tail.address = end_of_casrow;
    desc.push_back(tail);

    // init thd_ctx
    CasRow *mcasp = &(desc[0]);
    CasRow *last_row = &(desc[desc.size() - 2]);
    return invoke_mcas(thd_ctx, limbo_hdl, mcasp, last_row);
}

intptr_t mcas_read(MCasThreadCtx *thd_ctx, LimboHandle *limbo_hdl,
                   intptr_t *address)
{
    assert(thd_ctx->recur_depth == 0);
    intptr_t cvalue = *address;
    if (!is_mcas_helper(cvalue))
    {
        return cvalue;
    }
    else
    {
        MCasHelper *mch = (MCasHelper *)mcas_helper_unmask(cvalue);
        bool res = help_complete(thd_ctx, limbo_hdl, mch->cr);
        if (res && (mch->cr->mch == mch))
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
