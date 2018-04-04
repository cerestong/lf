#include "lf/logger.hh"

namespace lf
{

size_t Logger::kDoNotSupportGetLogFileSize = std::numeric_limits<size_t>::max();

Logger::~Logger() {}

void Logger::logv(const InfoLogLevel log_level, const char *format, va_list ap)
{
  static const char *kInfoLogLevelNames[5] = {"DEBUG", "INFO", "WARN",
                                              "ERROR", "FATAL"};
  if (log_level < log_level_)
  {
    return;
  }

  if (log_level == INFO_LEVEL)
  {
    // Doesn't print log level if it is INFO level.
    logv(format, ap);
  }
  else
  {
    char new_format[500];
    snprintf(new_format, sizeof(new_format) - 1, "[%s] %s",
             kInfoLogLevelNames[log_level], format);
    logv(new_format, ap);
  }
}

void header(Logger *info_log, const char *format, ...)
{
  if (info_log)
  {
    va_list ap;
    va_start(ap, format);
    info_log->log_header(format, ap);
    va_end(ap);
  }
}

void debug(Logger *info_log, const char *format, ...)
{
  if (info_log && info_log->get_info_log_level() <= DEBUG_LEVEL)
  {
    va_list ap;
    va_start(ap, format);
    info_log->logv(DEBUG_LEVEL, format, ap);
    va_end(ap);
  }
}

void info(Logger *info_log, const char *format, ...)
{
  if (info_log && info_log->get_info_log_level() <= INFO_LEVEL)
  {
    va_list ap;
    va_start(ap, format);
    info_log->logv(INFO_LEVEL, format, ap);
    va_end(ap);
  }
}

void warn(Logger *info_log, const char *format, ...)
{
  if (info_log && info_log->get_info_log_level() <= WARN_LEVEL)
  {
    va_list ap;
    va_start(ap, format);
    info_log->logv(WARN_LEVEL, format, ap);
    va_end(ap);
  }
}

void error(Logger *info_log, const char *format, ...)
{
  if (info_log && info_log->get_info_log_level() <= ERROR_LEVEL)
  {
    va_list ap;
    va_start(ap, format);
    info_log->logv(ERROR_LEVEL, format, ap);
    va_end(ap);
  }
}

void fatal(Logger *info_log, const char *format, ...)
{
  if (info_log && info_log->get_info_log_level() <= FATAL_LEVEL)
  {
    va_list ap;
    va_start(ap, format);
    info_log->logv(FATAL_LEVEL, format, ap);
    va_end(ap);
  }
}

// global logger
StdoutLogger g_stdout_logger;
bool g_stdout_logger_on = false;

void log(const char *format, ...)
{
  if (g_stdout_logger_on) {
    va_list ap;
    va_start(ap, format);
    g_stdout_logger.logv(format, ap);
    va_end(ap);
  }
}

} // end namespace
