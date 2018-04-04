#pragma once

#include <cstdarg>
#include <limits>
#include <stdio.h>
#include <memory>

namespace lf
{

enum InfoLogLevel
{
  DEBUG_LEVEL = 0,
  INFO_LEVEL,
  WARN_LEVEL,
  ERROR_LEVEL,
  FATAL_LEVEL,
  HEADER_LEVEL,
  NUM_INFO_LOG_LEVELS,
};

// An interface for writing log messages.
class Logger
{
public:
  static size_t kDoNotSupportGetLogFileSize;

  explicit Logger(const InfoLogLevel log_level = INFO_LEVEL)
      : log_level_(log_level) {}
  virtual ~Logger();

  // Write a header to the log file with the specified format
  // It is recommended that you log all header information at the start of the
  // application. But it is not enforced.
  virtual void log_header(const char *format, va_list ap)
  {
    // Default implementation does a simple INFO level log write.
    // Please override as per the logger class requirement.
    logv(format, ap);
  }

  // Write an entry to the log file with the specified format.
  virtual void logv(const char *format, va_list ap) = 0;

  // Write an entry to the log file with the specified log level
  // and format.  Any log with level under the internal log level
  // of *this (see @SetInfoLogLevel and @GetInfoLogLevel) will not be
  // printed.
  virtual void logv(const InfoLogLevel log_level, const char *format, va_list ap);

  virtual size_t get_log_file_size() const { return kDoNotSupportGetLogFileSize; }
  // Flush to the OS buffers
  virtual void flush() {}
  virtual InfoLogLevel get_info_log_level() const { return log_level_; }
  virtual void set_info_log_level(const InfoLogLevel log_level)
  {
    log_level_ = log_level;
  }

private:
  // No copying allowed
  Logger(const Logger &);
  void operator=(const Logger &);
  InfoLogLevel log_level_;
};

// a set of log functions with different log levels.
extern void header(Logger *info_log, const char *format, ...);
extern void debug(Logger *info_log, const char *format, ...);
extern void info(Logger *info_log, const char *format, ...);
extern void warn(Logger *info_log, const char *format, ...);
extern void error(Logger *info_log, const char *format, ...);
extern void fatal(Logger *info_log, const char *format, ...);

class StdoutLogger : public Logger
{
public:
  StdoutLogger()
      : Logger(DEBUG_LEVEL)
  {
  }
  using Logger::logv;
  virtual void logv(const char *format, va_list ap)
  {
    vprintf(format, ap);
    printf("\n");
  }
};

extern StdoutLogger g_stdout_logger;
extern bool g_stdout_logger_on;

extern void log(const char *format, ...);

class Status;

extern Status new_logger(const std::string &fname,
                         std::shared_ptr<Logger> *logger);

} // end namespace lf
