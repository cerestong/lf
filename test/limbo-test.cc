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

/*
use single atomic var for transaction id
使用atomic<uint64_t>管理事务id，创建新事务原子添加id，释放内存时原子的获取最新的id

thread0 loop_cnt 20000000 in 3761155 micros, 5.31752e+06/s
total loop_cnt 20000000 in 3761155 micros, 5.31752e+06/s
thread0 loop_cnt 20000000 in 6188811 micros, 3.23164e+06/s
thread1 loop_cnt 20000000 in 6520168 micros, 3.06741e+06/s
total loop_cnt 40000000 in 6520235 micros, 6.13475e+06/s
thread0 loop_cnt 20000000 in 6140409 micros, 3.25711e+06/s
thread2 loop_cnt 20000000 in 7183240 micros, 2.78426e+06/s
thread1 loop_cnt 20000000 in 7264088 micros, 2.75327e+06/s
total loop_cnt 60000000 in 7274400 micros, 8.2481e+06/s
thread0 loop_cnt 20000000 in 9387626 micros, 2.13046e+06/s
thread3 loop_cnt 20000000 in 9753296 micros, 2.05059e+06/s
thread1 loop_cnt 20000000 in 10019251 micros, 1.99616e+06/s
thread2 loop_cnt 20000000 in 10059883 micros, 1.98809e+06/s
total loop_cnt 80000000 in 10066607 micros, 7.94707e+06/s


忽略内存分配函数
thread0 loop_cnt 20000000 in 2674568 micros, 7.47784e+06/s
total loop_cnt 20000000 in 2674568 micros, 7.47784e+06/s
thread1 loop_cnt 20000000 in 5414358 micros, 3.69388e+06/s
thread0 loop_cnt 20000000 in 5422092 micros, 3.68861e+06/s
total loop_cnt 40000000 in 5422092 micros, 7.37723e+06/s
thread0 loop_cnt 20000000 in 5475562 micros, 3.65259e+06/s
thread2 loop_cnt 20000000 in 6323946 micros, 3.16258e+06/s
thread1 loop_cnt 20000000 in 6475865 micros, 3.08839e+06/s
total loop_cnt 60000000 in 6481451 micros, 9.25719e+06/s
thread0 loop_cnt 20000000 in 8332756 micros, 2.40017e+06/s
thread3 loop_cnt 20000000 in 8573943 micros, 2.33265e+06/s
thread1 loop_cnt 20000000 in 8877917 micros, 2.25278e+06/s
thread2 loop_cnt 20000000 in 8896292 micros, 2.24813e+06/s
total loop_cnt 80000000 in 8896298 micros, 8.9925e+06/s
可以明显观察到atomic对并发的影响，四个线程的性能相对三个线程还有下滑。
*/

struct Ctx
{
    uint64_t begin;
    uint64_t end;
    int64_t loop_cnt;
    lf::ThreadInfo *ti;
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
            ctx->ti->delete_handle(handle[i % 2]);
        
        handle[i % 2] = ctx->ti->new_handle();

        char *buf = (char *)handle[i % 2]->alloc(64);
        buf[0] = 'a';
        handle[i % 2]->dealloc(buf);
    }
    ctx->ti->delete_handle( handle[0] );
    ctx->ti->delete_handle( handle[1] );

    ctx->end = lf::now_micros();

    double d = ((double)(ctx->end - ctx->begin)) * 1e-6;

    lf::log("thread%d loop_cnt %lld in %lld micros, %g/s",
            ctx->i, ctx->loop_cnt, ctx->end - ctx->begin,
            (double)(ctx->loop_cnt) / d);
}

void multi_thread_test(int thd_no)
{
    lf::g_all_threads = new std::vector<lf::ThreadInfo>(thd_no);
    Ctx *ctx = new Ctx[thd_no];
    uint64_t min_begin = 0xffffffffffffffffUL;
    uint64_t max_end = 0;
    int64_t total_loop = 0;

    for (int i = 0; i < thd_no; i++)
    {
        ctx[i].i = i;
        ctx[i].loop_cnt = 2e7;
        ctx[i].ti = &((*lf::g_all_threads)[i]);
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
    multi_thread_test(4);    

    return 0;
}
