#pragma once

#include <list>
#include <string>
#include <memory>
#include <mutex>
#include "lf/logger.hh"
#include "lf/status.hh"

namespace lf
{

class AutoRollLogger : public Logger
{
private:
  std::string log_fname_;
  std::string log_dir_;
  //std::string absolute_path_;
  std::shared_ptr<Logger> logger_;
  Status status_;
  const size_t kMaxLogFileSize;
  const size_t kLogFileTimeToRoll;
  std::list<std::string> headers_;
  // to avoid frequent env->NowMicros() calls, cached the current time
  uint64_t cached_now;
  uint64_t ctime_;
  uint64_t cached_now_access_count;
  uint64_t call_NowMicros_every_N_records_;
  mutable std::mutex mutex_;

public:
  AutoRollLogger(const std::string &log_dir,
                 size_t log_max_size,
                 size_t log_file_time_to_roll,
                 const InfoLogLevel log_level = INFO_LEVEL);

  virtual ~AutoRollLogger()
  {
  }

  using Logger::logv;
  virtual void logv(const char *format, va_list ap);

  virtual void log_header(const char *format, va_list ap);

  Status get_status() { return status_; }

  virtual size_t get_log_file_size() const;

  virtual void flush();

  void set_call_nowmicros_every_nrecords(uint64_t call_NowMicros_every_N_records)
  {
    call_NowMicros_every_N_records_ = call_NowMicros_every_N_records;
  }

  std::string TEST_log_fname() const
  {
    return log_fname_;
  }

  static std::string log_filename(const std::string &log_dir);
  static std::string old_log_filename(uint64_t ts, const std::string &log_dir);

private:
  bool log_expired();
  Status reset_logger();
  void roll_log_file();
  void log_internal(const char *format, ...);
  std::string valist_to_string(const char *format, va_list args) const;
  void write_header_info();
};

} // end namespace
