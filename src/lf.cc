#include "lf/lf.hh"
#include "lf/limbo.hh"
#include "lf/wfmcas.hh"

namespace lf {
    Status init_lf_library(size_t work_thread_no)
    {
        static bool bInited = false;
        assert(bInited == false);

        bInited = true;
        g_all_threads = new std::vector<ThreadInfo>(work_thread_no);
        Status ret = init_wfmcas(work_thread_no);

        return ret;
    }

    void deinit_lf_library()
    {
        if (g_all_threads)
        {
            for (size_t i = 0; i < g_all_threads->size(); i++)
            {
                ThreadInfo &ti = (*g_all_threads)[i];
                ti.destroy();
            }
            delete g_all_threads;
            g_all_threads = nullptr;
        }
    }    
} // end namespace
