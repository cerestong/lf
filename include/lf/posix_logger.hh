#pragma once

#include "lf/logger.hh"
#include "lf/env_util.hh"
#include <assert.h>

namespace lf
{

const int kDebugLogChunkSize = 128 * 1024;

class PosixLogger : public Logger
{
  private:
	FILE *file_;
	size_t log_size_;
	int fd_;
	const static uint64_t flush_every_seconds_ = 5;
	uint64_t last_flush_micros_;
	bool flush_pending_;

  public:
	PosixLogger(FILE *f, const InfoLogLevel log_level = ERROR_LEVEL)
		: Logger(log_level),
		  file_(f),
		  log_size_(0),
		  fd_(fileno(f)),
		  last_flush_micros_(0),
		  flush_pending_(false)
	{
	}
	virtual ~PosixLogger()
	{
		fclose(file_);
	}
	virtual void flush()
	{
		if (flush_pending_)
		{
			flush_pending_ = false;
			fflush(file_);
		}
		last_flush_micros_ = now_micros();
	}

	using Logger::logv;
	virtual void logv(const char *format, va_list ap)
	{
		const uint64_t thread_id = get_tid();

		// We try twice: the first time with a fixed-size stack allocated buffer,
		// and the second time with a much larger dynamically allocated buffer.
		char buffer[500];
		for (int iter = 0; iter < 2; iter++)
		{
			char *base;
			int bufsize;
			if (iter == 0)
			{
				bufsize = sizeof(buffer);
				base = buffer;
			}
			else
			{
				bufsize = 30000;
				base = new char[bufsize];
			}
			char *p = base;
			char *limit = base + bufsize;

			struct tm t;
			int32_t usec = now_tm(&t);
			p += snprintf(p, limit - p,
						  "%04d/%02d/%02d-%02d:%02d:%02d.%06d %llx ",
						  t.tm_year + 1900,
						  t.tm_mon + 1,
						  t.tm_mday,
						  t.tm_hour,
						  t.tm_min,
						  t.tm_sec,
						  usec,
						  static_cast<long long unsigned int>(thread_id));

			// Print the message
			if (p < limit)
			{
				va_list backup_ap;
				va_copy(backup_ap, ap);
				p += vsnprintf(p, limit - p, format, backup_ap);
				va_end(backup_ap);
			}

			// Truncate to available space if necessary
			if (p >= limit)
			{
				if (iter == 0)
				{
					continue; // Try again with larger buffer
				}
				else
				{
					p = limit - 1;
				}
			}

			// Add newline if necessary
			if (p == base || p[-1] != '\n')
			{
				*p++ = '\n';
			}

			assert(p <= limit);
			const size_t write_size = p - base;

			size_t sz = fwrite(base, 1, write_size, file_);
			flush_pending_ = true;
			assert(sz == write_size);
			if (sz > 0)
			{
				// 多线程环境可能计数不准确，但不影响使用
				log_size_ += write_size;
			}
			if (now_micros() - last_flush_micros_ >= flush_every_seconds_ * 1000000)
			{
				flush();
			}
			if (base != buffer)
			{
				delete[] base;
			}
			break;
		}
	}
	size_t get_log_file_size() const { return log_size_; }
};


} // end namespace lf
