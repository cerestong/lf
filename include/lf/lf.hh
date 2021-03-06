#pragma once
#include "lf/status.hh"

namespace lf {
    
    Status init_lf_library(size_t work_thread_no);

    void deinit_lf_library();

} // end namespace;
