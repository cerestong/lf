#include <pthread.h>
#include <string.h>
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>
#include "lf/env_util.hh"

namespace lf
{

uint64_t get_tid()
{
    uint64_t thread_id = 0;
    pthread_t tid = pthread_self();
    memcpy(&thread_id, &tid, std::min(sizeof(thread_id), sizeof(tid)));
    return thread_id;
}

bool dir_exists(const std::string &dname)
{
    struct stat statbuf;
    if (stat(dname.c_str(), &statbuf) == 0)
    {
        return S_ISDIR(statbuf.st_mode);
    }
    return false;
}

Status create_dir_if_missing(const std::string &name)
{
    Status result;
    if (mkdir(name.c_str(), 0755) != 0)
    {
        if (errno != EEXIST)
        {
            result = Status::IOError(name, strerror(errno));
        }
        else if (!dir_exists(name))
        {
            result = Status::IOError("`" + name + "` exists but is not a directory");
        }
    }
    return result;
}

Status file_exists(const std::string &fname)
{
    int result = access(fname.c_str(), F_OK);
    if (0 == result)
        return Status::OK();

    switch (errno)
    {
    case EACCES:
    case ELOOP:
    case ENAMETOOLONG:
    case ENOENT:
    case ENOTDIR:
        return Status::NotFound();
    default:
        assert(result == EIO || result == ENOMEM);
        std::string tmp;
        Slice::set_number((int64_t)result, &tmp);
        return Status::IOError("Unexpected error(" + tmp +
                               ") accessing file `" + fname + "`");
    }
}

Status rename_file(const std::string &ofname, const std::string &nfname)
{
    Status result;
    if (rename(ofname.c_str(), nfname.c_str()) != 0)
        result = Status::IOError(ofname, strerror(errno));
    return result;
}
}