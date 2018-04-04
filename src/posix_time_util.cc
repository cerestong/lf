#include "lf/time_util.hh"
#include <sys/time.h>

namespace lf
{

uint64_t now_micros()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

int32_t now_tm(struct tm *t)
{
    struct timeval now_tv;
    gettimeofday(&now_tv, nullptr);
    const time_t seconds = now_tv.tv_sec;
    localtime_r(&seconds, t);
    return static_cast<int32_t>(now_tv.tv_usec);
}

} // end namespace
