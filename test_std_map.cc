#include <map>
#include <string>
#include <thread>
#include <vector>
#include <stdint.h>
#include <mutex>

void base_test(std::map<std::string, std::string> &m)
{
    m.insert(std::pair<std::string, std::string>("key1", "v1"));
    m.insert(std::pair<std::string, std::string>("key1", "v1"));
    
    m.find("key1");
    m.erase("key1");
    m.erase("key1");

    m.insert(std::pair<std::string, std::string>("key1", "v1"));
    m.erase("key1");    
}

void single_thread_base_test(int loops)
{
    std::map<std::string, std::string> m;
    
    for (int i = 0; i < 10000000; i++) {
        base_test(m);
    }
}

std::mutex g_mtx;

void thd_enter(std::map<int32_t, int32_t> *m, int from, int cnt)
{
    for (int i = 0; i < cnt; i++) {
        int32_t key = from + i;
        std::lock_guard<std::mutex> guard(g_mtx);
        m->insert(std::pair<int32_t, int32_t>(key, from));
    }
}

void multi_thread_base_test(int thdno)
{
    std::map<int32_t, int32_t> m;
    std::vector<std::thread *> thds;

    for (int i =0; i < thdno; i++) {
        std::thread *thd = new std::thread(std::bind(&thd_enter, &m, i*100000-5000, 100000));
        thds.push_back(thd);
    }

    for (int i = 0; i < thdno; i++) {
        thds[i]->join();
        delete thds[i];
    }
}

int main(int argc, char **argv)
{

    multi_thread_base_test(5);
    return 0;
}