#include "rocksdb/db.h"
#include "lf/logger.hh"
#include <cassert>

std::string kDBPath = "/home/tongxingguo/work/lf/mybuild/testdb";
std::string ikey(int i)
{
    char buf[16] = {'\0'};
    int len = snprintf(buf, sizeof(buf), "key%06d", i);
    return std::string(buf, len);
}

std::string ivalue(int i)
{
    char buf[16] = {'\0'};
    int len = snprintf(buf, sizeof(buf), "value%06d", i);
    return std::string(buf, len);
}

void print_db(rocksdb::DB *db)
{
    using namespace rocksdb;
    Iterator *ite = db->NewIterator(ReadOptions());
    lf::log("{");
    for (ite->SeekToFirst(); ite->Valid(); ite->Next())
    {
        Slice k = ite->key();
        Slice v = ite->value();
        lf::log("\t%*s: %*s", k.size(), k.data(), v.size(), v.data());
    }
    lf::log("}");

    delete ite;
}

// atomically apply a set of updates
void write_batch(rocksdb::DB *db)
{
    using namespace  rocksdb;
    WriteBatch batch;
    batch.Delete(ikey(1));
    batch.Put(ikey(2), ivalue(2));
    Status s = db->Write(WriteOptions(), &batch);
    if (!s.ok())
        lf::log("%s failed: %s", __FUNCTION__, s.ToString().c_str());
}

// 默认是异步写，write到系统缓存就返回。
// 使用sync标记，可以在同步到磁盘后返回
//（fsync, or fdatasync or msync(..., MS_SYNC)）
void sync_write(rocksdb::DB *db)
{
    using namespace rocksdb;
    WriteOptions option;
    option.sync = true;
    Status s;

    s = db->Put(option, ikey(4), ivalue(4));
    if (s.ok())
    {
        lf::log("Sync Put : %s", ikey(4).c_str());
    }
    else
    {
        lf::log("Sync Put failed: %s", s.ToString().c_str());
    }

    // disableWAL
    // option.disableWAL = true;

}

void read_and_write(rocksdb::DB *db)
{
    using namespace rocksdb;
    std::string value;

    Status s;
    s = db->Get(ReadOptions(), ikey(2), &value);
    if (s.ok())
    {
        lf::log("Get %s : %s", ikey(2).c_str(), value.c_str());
        s = db->Put(WriteOptions(), ikey(1), value);
        if (s.ok())
        {
            s = db->Delete(WriteOptions(), ikey(2));
            lf::log("Delete %s", ikey(2).c_str());
        }

        PinnableSlice pinnable_value;
        db->Get(ReadOptions(), db->DefaultColumnFamily(), ikey(1), &pinnable_value);
        lf::log("pinable_val : %*s", pinnable_value.size(), pinnable_value.data());
    }
    else
    {
        s = db->Put(WriteOptions(), ikey(2), ivalue(2));
        if (s.ok())
        {
            lf::log("put %s : %s", ikey(2).c_str(), ivalue(2).c_str());
        }
        else
        {
            lf::log("put %s failed: %s", ikey(2).c_str(), s.ToString().c_str());
        }
    }
}

void hello_world()
{
    rocksdb::DB *db;
    rocksdb::Options options;
    // rocksDB by default uses faster fdatasync() to sync files.
    // use fsync() to sync files;
    //options.use_fsync = true;
    options.IncreaseParallelism();
    options.OptimizeLevelStyleCompaction();    
    options.create_if_missing = true;
    rocksdb::Status s = rocksdb::DB::Open(options, kDBPath, &db);
    assert(s.ok());
    // write_buffer_size 配置后 read_and_write()用例会崩溃。
    //s = db->SetOptions({{"write_buffer_size", "131072"}});
    // assert(s.ok());
    // s = db->SetDBOptions({{"max_background_flushes", "2"}});
    if (!s.ok())
    {
        lf::log("%s", s.ToString().c_str());
    }
    assert(s.ok());

    //read_and_write(db);
    //write_batch(db);
    sync_write(db);

    print_db(db);

    delete db;
    db = nullptr;
}

int main(int argc, char **argv)
{
    lf::g_stdout_logger_on = true;
    hello_world();
    return 0;
}