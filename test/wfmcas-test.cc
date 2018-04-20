#include "lf/lf.hh"
#include "lf/logger.hh"
#include "lf/wfmcas.hh"
#include "lf/time_util.hh"
#include <thread>

intptr_t g_array[2] = {0};

struct Ctx
{
    uint64_t begin;
    uint64_t end;
    int64_t loop_cnt;
    lf::ThreadInfo *ti;
    lf::MCasThreadCtx *mcas_ctx;
    std::thread *thd;
    int i;
};

void single_thread_test(Ctx *ctx)
{
    ctx->begin = lf::now_micros();

    lf::LimboHandle *handle = nullptr;
    for (int64_t i = 1; i <= ctx->loop_cnt; i++)
    {
        handle = ctx->ti->new_handle();

        std::vector<lf::CasRow> desc;
        intptr_t g1 = lf::mcas_read(ctx->mcas_ctx, handle, g_array);
        intptr_t g2 = lf::mcas_read(ctx->mcas_ctx, handle, g_array + 1);
        desc.push_back(lf::CasRow(g_array, g1, i * (ctx->i+1)));
        desc.push_back(lf::CasRow(g_array+1, g2, i * (ctx->i+1)));


        bool ret = lf::mcas(ctx->mcas_ctx, handle, desc);

        ctx->ti->delete_handle(handle);
    }

    ctx->end = lf::now_micros();

    double d = ((double)(ctx->end - ctx->begin)) * 1e-6;

    lf::log("thread%d loop_cnt %lld in %lld micros, %g/s",
            ctx->i, ctx->loop_cnt, ctx->end - ctx->begin,
            (double)(ctx->loop_cnt) / d);
}

void multi_thread_test(int thd_no)
{
    Ctx *ctx = new Ctx[thd_no];
    uint64_t min_begin = (uint64_t)-1;
    uint64_t max_end = 0;
    int64_t total_loop = 0;

    for (int i = 0; i < thd_no; i++)
    {
        ctx[i].i = i;
        ctx[i].loop_cnt = 2e6;
        ctx[i].ti = &((*lf::g_all_threads)[i]);
        ctx[i].mcas_ctx = lf::init_mcas_thread_ctx(i);
        ctx[i].thd = new std::thread(single_thread_test, ctx + i);
    }

    for (int i = 0; i < thd_no; i++)
    {
        ctx[i].thd->join();
        delete ctx[i].thd;
        if (ctx[i].begin < min_begin)
            min_begin = ctx[i].begin;
        if (ctx[i].end > max_end)
            max_end = ctx[i].end;
        total_loop += ctx[i].loop_cnt;
    }

    double d = ((double)(max_end - min_begin)) * 1e-6;

    lf::log("total loop_cnt %lld in %lld micros, %g/s",
            total_loop, max_end - min_begin,
            (double)total_loop / d);

    lf::log("g_array(%ld, %ld)", g_array[0], g_array[1]);

    delete[] ctx;
}

int main(int argc, char **argv)
{
    int work_thread_no = 4;
    lf::Status sts = lf::init_lf_library(work_thread_no);

    lf::g_stdout_logger_on = true;
    multi_thread_test(work_thread_no);

    return 0;
}
