#include <thread>
#include "lf/time_util.hh"
#include "lf/logger.hh"
#include "lf/masstree.hh"
#include "lf/lf.hh"

using namespace lf;

// namespace lf {
//     uint64_t initial_timestamp = 0;
// }

struct Ctx
{
    uint64_t begin;
    uint64_t end;
    int64_t loop_cnt;
    lf::ThreadInfo *ti;
    std::thread *thd;
    int i;
};

class PrintHelper
{
  public:
    static int unparse_key(lf::MtKey key, char *buf, int buflen)
    {
        return key.unparse(buf, buflen);
    }
    static void print(LeafValue val, FILE *f, const char *prefix,
                      int indent, Slice key, uint64_t initial_timestamp, char *suffix)
    {
        lf::ValuePrint<uint64_t>::print( val.value(), f, prefix, indent, key, initial_timestamp, suffix);
    }
};

struct ScanTester
{
    Slice vbegin_;
    Slice vend_;
    bool first_;

    ScanTester(const Slice &vbegin, const Slice &vend)
        : vbegin_(vbegin), vend_(vend), first_(true)
    {
    }

    void visit_leaf(const ScanStackElt&, const MtKey&, ThreadInfo *)
    {
    }

    bool visit_value(Slice key, LeafValue &val, ThreadInfo *)
    {
        fprintf(stderr, "scan %.*s\n", (int)(key.size()), key.data());
        return key.compare(vend_) < 0 ? true : false; 
    }

    int scan(BasicTable &table, ThreadInfo *ti)
    {
        return table.scan(vbegin_, first_, *this, ti);
    }
};

class MasstreeWrapper
{
  public:
    struct TableParams : public lf::NodeParams
    {
        typedef PrintHelper key_unparse_type;
        typedef PrintHelper value_print_type;
    };

    BasicTable table_;

    void table_init(lf::ThreadInfo *ti)
    {
        table_.initialize(ti);
    }

    void table_destroy(lf::ThreadInfo *ti)
    {
        table_.destroy(ti);
    }

    bool insert(uint64_t k, intptr_t val, ThreadInfo *ti)
    {
        char key_buf[16];
        Slice key = make_key(k, key_buf, sizeof(key_buf));
        TCursor lp(table_, key);
        bool found = lp.find_insert(ti);
        if (!found)
        {
            lp.value() = val;

            compiler_barrier();
            lp.finish(1, ti);
        }
        else
        {
            lp.finish(0, ti);
        }
        return !found;
    }

    void my_insert(uint64_t begin, uint64_t end, int delta, ThreadInfo *ti)
    {
        lf::LimboHandle *handle = nullptr;
        handle = ti->new_handle();

        if (delta > 0)
        {
            assert(begin < end);
            while (begin < end)
            {
                insert(begin, begin, ti);
                begin += delta;
            }
        }
        else
        {
            assert(begin > end);
            while (begin > end)
            {
                insert(begin, begin, ti);

                begin += delta;
            }
        }

        ti->delete_handle(handle);
    }

    bool remove(uint64_t k, ThreadInfo *ti)
    {
        char key_buf[16];
        Slice key = make_key(k, key_buf, sizeof(key_buf));
        TCursor lp(table_, key);
        bool found = lp.find_locked(ti);
        if (found)
        {
            // add delete value ope
            lp.finish(-1, ti);
        }
        else
        {
            lp.finish(0, ti);
        }
        return found;
    }

    void my_remove(uint64_t begin, uint64_t end, int delta, ThreadInfo *ti)
    {
        lf::LimboHandle *handle = nullptr;
        handle = ti->new_handle();

        if (delta > 0)
        {
            assert(begin < end);
            while (begin < end)
            {
                remove(begin, ti);
                begin += delta;
            }
        }
        else
        {
            assert(begin > end);
            while (begin > end)
            {
                remove(begin, ti);

                begin += delta;
            }
        }

        ti->delete_handle(handle);
    }

    void thread_entry(Ctx *ctx)
    {
        uint64_t int_key = 123456789012L;

        ctx->begin = lf::now_micros();

        //my_insert(int_key, int_key + ctx->loop_cnt, 1 /*ctx->i+1*/, ctx->ti);
        //my_remove(int_key, int_key + ctx->loop_cnt, 1 /*ctx->i+1*/, ctx->ti);

        my_insert(int_key, int_key + 500, 5, ctx->ti);

        ctx->end = lf::now_micros();
        table_.print<TableParams>(stdout);

        ScanTester st(Slice("123456789012"), Slice("123456790000"));
        st.scan(table_, ctx->ti);
    }

    static inline lf::Slice make_key(uint64_t int_key, char *key_buf, size_t key_buf_len)
    {
        int n = snprintf(key_buf, key_buf_len, "%lu", int_key);
        return lf::Slice((const char *)key_buf, n);
    }
};

void multi_thread_test(int thd_no)
{
    MasstreeWrapper mw;

    lf::init_lf_library(thd_no);
    mw.table_init(&((*lf::g_all_threads)[0]));

    Ctx *ctx = new Ctx[thd_no];
    uint64_t min_begin = 0xffffffffffffffffUL;
    uint64_t max_end = 0;
    int64_t total_loop = 0;

    for (int i = 0; i < thd_no; i++)
    {
        ctx[i].i = i;
        ctx[i].loop_cnt = 1e6;
        ctx[i].ti = &((*lf::g_all_threads)[i]);
        ctx[i].thd = new std::thread(std::bind(&MasstreeWrapper::thread_entry, &mw, ctx + i));
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

    mw.table_destroy(ctx[0].ti);
    // 主动触发一次内存回�?
    ctx[0].ti->hard_free();

    double d = ((double)(max_end - min_begin)) * 1e-6;

    lf::log("total loop_cnt %lld in %lld micros, %g/s",
            total_loop, max_end - min_begin,
            (double)total_loop / d);

    delete[] ctx;
    lf::deinit_lf_library();
}

int main(int argc, char *argv[])
{
    lf::g_stdout_logger_on = true;
    multi_thread_test(1);

    return 0;
}