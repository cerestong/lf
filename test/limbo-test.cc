#include "lf/limbo.hh"
#include "lf/time_util.hh"
#include "lf/logger.hh"
#include <thread>

/*
因为全局锁的原因，增加线程数量并没有提高系统吞吐量。
甚至因为全局锁的竞争，整体的吞吐量甚至有了下降。

thread0 loop_cnt 10000000 in 4742497 micros, 2.10859e+06/s
total loop_cnt 10000000 in 4742497 micros, 2.10859e+06/s
thread1 loop_cnt 10000000 in 17011600 micros, 587834/s
thread0 loop_cnt 10000000 in 17214748 micros, 580897/s
total loop_cnt 20000000 in 17214748 micros, 1.16179e+06/s
thread1 loop_cnt 10000000 in 27561840 micros, 362820/s
thread0 loop_cnt 10000000 in 28082906 micros, 356089/s
thread2 loop_cnt 10000000 in 28104555 micros, 355814/s
total loop_cnt 30000000 in 28106120 micros, 1.06738e+06/s

*/

struct Ctx
{
    uint64_t begin;
    uint64_t end;
    int64_t loop_cnt;
    lf::Limbo *limbo;
    std::thread *thd;
    int i;
};

void single_thread_test(Ctx *ctx)
{
    ctx->begin = lf::now_micros();

    lf::LimboHandle *handle[2] = {nullptr, nullptr};
    for (int64_t i = 0; i < ctx->loop_cnt; i++)
    {
        if (handle[i % 2])
            delete handle[i % 2];
        handle[i % 2] = ctx->limbo->new_handle();

        char *buf = (char *)handle[i % 2]->alloc(64);
        buf[0] = 'a';
        handle[i % 2]->dealloc(buf);
    }
    delete handle[0];
    delete handle[1];

    ctx->end = lf::now_micros();

    double d = ((double)(ctx->end - ctx->begin)) * 1e-6;

    lf::log("thread%d loop_cnt %lld in %lld micros, %g/s",
            ctx->i, ctx->loop_cnt, ctx->end - ctx->begin,
            (double)(ctx->loop_cnt) / d);
}

void multi_thread_test(int thd_no)
{
    lf::Limbo limbo;
    Ctx *ctx = new Ctx[thd_no];
    uint64_t min_begin = 0xffffffffffffffffUL;
    uint64_t max_end = 0;
    int64_t total_loop = 0;

    for (int i = 0; i < thd_no; i++)
    {
        ctx[i].i = i;
        ctx[i].loop_cnt = 1e7;
        ctx[i].limbo = &limbo;
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

    delete[] ctx;
}

int main(int argc, char *argv[])
{
    lf::g_stdout_logger_on = true;
    multi_thread_test(1);
    multi_thread_test(2);
    multi_thread_test(3);    

    return 0;
}
