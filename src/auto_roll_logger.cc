#include <stdio.h>
#include "lf/env_util.hh"
#include "lf/auto_roll_logger.hh"

namespace lf
{

AutoRollLogger::AutoRollLogger(
    const std::string &log_dir,
    size_t log_max_size,
    size_t log_file_time_to_roll,
    const InfoLogLevel log_level)
    : Logger(log_level),
      log_dir_(log_dir),
      status_(Status::OK()),
      kMaxLogFileSize(log_max_size),
      kLogFileTimeToRoll(log_file_time_to_roll),
      cached_now(static_cast<uint64_t>(now_micros() * 1e-6)),
      ctime_(cached_now),
      cached_now_access_count(0),
      call_NowMicros_every_N_records_(100),
      mutex_()
{
  log_fname_ = log_filename(log_dir_);
  roll_log_file();
  reset_logger();
}

std::string AutoRollLogger::log_filename(const std::string &log_dir)
{
  if (log_dir.empty())
  {
    return "./LOG";
  }

  return log_dir + "/LOG";
}

std::string AutoRollLogger::old_log_filename(uint64_t ts, const std::string &log_dir)
{
  char buf[50];
  const time_t seconds = ts / 1e6;
  struct tm t;
  localtime_r(&seconds, &t);
  snprintf(buf,
           sizeof(buf),
           "%04d%02d%02d-%02d%02d%02d.%06d",
           t.tm_year + 1900,
           t.tm_mon + 1,
           t.tm_mday,
           t.tm_hour,
           t.tm_min,
           t.tm_sec,
           static_cast<int>(ts % 1000000));

  if (log_dir.empty())
  {
    return std::string("./LOG.old.") + buf;
  }

  return log_dir + "/LOG.old." + buf;
}

void AutoRollLogger::roll_log_file()
{
  uint64_t now = now_micros();
  std::string old_fname;
  do
  {
    old_fname = old_log_filename(now, log_dir_);
    now++;
  } while (file_exists(old_fname).ok());
  rename_file(log_fname_, old_fname);
}

Status AutoRollLogger::reset_logger()
{
  status_ = new_logger(log_fname_, &logger_);

  if (!status_.ok())
  {
    return status_;
  }

  if (logger_->get_log_file_size() == Logger::kDoNotSupportGetLogFileSize)
  {
    status_ = Status::NotSupported(
        "The underlying logger doesn't support GetLogFileSize()");
  }
  if (status_.ok())
  {
    cached_now = static_cast<uint64_t>(now_micros() * 1e-6);
    ctime_ = cached_now;
    cached_now_access_count = 0;
  }

  return status_;
}

std::string AutoRollLogger::valist_to_string(const char *format,
                                             va_list args) const
{
  // Any log messages longer than 1024 will get truncated.
  // The user is responsible for chopping longer messages into multi line log
  static const int MAXBUFFERSIZE = 1024;
  char buffer[MAXBUFFERSIZE];

  int count = vsnprintf(buffer, MAXBUFFERSIZE, format, args);
  (void)count;
  // assert(count >= 0);

  return buffer;
}

void AutoRollLogger::log_internal(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  logger_->logv(format, args);
  va_end(args);
}

void AutoRollLogger::logv(const char *format, va_list ap)
{
  std::shared_ptr<Logger> logger(logger_);
  if (logger)
  {
    logger->logv(format, ap);
    ++cached_now_access_count;
  }

  if ((!logger) ||
      (kLogFileTimeToRoll > 0 && log_expired()) ||
      (kMaxLogFileSize > 0 && logger->get_log_file_size() >= kMaxLogFileSize))
  {
    std::lock_guard<std::mutex> lck(mutex_);
    if ((!logger_) ||
        (kLogFileTimeToRoll > 0 && log_expired()) ||
        (kMaxLogFileSize > 0 && logger_->get_log_file_size() >= kMaxLogFileSize))
    {
      roll_log_file();
      Status s = reset_logger();
      if (!s.ok())
      {
        // can't really log the error if creating a new LOG file failed
        return;
      }

      write_header_info();
    }
  }
}

void AutoRollLogger::write_header_info()
{
  for (std::list<std::string>::iterator ite = headers_.begin();
       ite != headers_.end();
       ite++)
  {
    std::string &header = *ite;
    log_internal("%s", header.c_str());
  }
}

void AutoRollLogger::log_header(const char *format, va_list args)
{
  // header message are to be retained in memory. Since we cannot make any
  // assumptions about the data contained in va_list, we will retain them as
  // strings
  va_list tmp;
  va_copy(tmp, args);
  std::string data = valist_to_string(format, tmp);
  va_end(tmp);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    headers_.push_back(data);
  }

  // Log the original message to the current log
  std::shared_ptr<Logger> logger(logger_);
  if (logger) logger->logv(format, args);
}

bool AutoRollLogger::log_expired()
{
  if (cached_now_access_count >= call_NowMicros_every_N_records_)
  {
    cached_now = static_cast<uint64_t>(now_micros() * 1e-6);
    cached_now_access_count = 0;
  }

  return cached_now >= ctime_ + kLogFileTimeToRoll;
}

size_t AutoRollLogger::get_log_file_size() const
{
  std::shared_ptr<Logger> logger(logger_);
  return logger ? logger->get_log_file_size() : 0;
}

void AutoRollLogger::flush()
{
  std::shared_ptr<Logger> logger(logger_);
  if (logger)
  {
    logger->flush();
  }
}

} // end namespace
