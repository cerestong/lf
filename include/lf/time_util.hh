#pragma once

#include <stdint.h>
#include <time.h>

namespace lf
{

uint64_t now_micros();

// local_time的tm结构，并返回usec
int32_t now_tm(struct tm *t);

} // end namespace