#pragma once

#include "lf/time_util.hh"
#include "lf/status.hh"

namespace lf
{
uint64_t get_tid();

bool dir_exists(const std::string &dname);

Status create_dir_if_missing(const std::string &name);

Status file_exists(const std::string &fname);

Status rename_file(const std::string &ofname, const std::string &nfname);
} // end namespace
