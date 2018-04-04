#include "lf/posix_logger.hh"
#include <fcntl.h>

namespace lf
{

Status new_logger(const std::string &fname,
                  std::shared_ptr<Logger> *logger)
{
    FILE *f;
    f = fopen(fname.c_str(), "w");
    if (f == nullptr)
    {
        logger->reset();
        return Status::IOError(fname, strerror(errno));
    }
    else
    {
        int fd = fileno(f);
        fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
        logger->reset(new PosixLogger(f));
        return Status::OK();
    }
}

} // end namespace
