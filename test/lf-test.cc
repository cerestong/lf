#include "lf/hash_map.hh"
#include <assert.h>
#include <string>
#include <iostream>
#include <thread>

std::string values[] = {
    "key1:v1",
    "key2:v2",
    "key3:v3",
    "key4:v4"};

void base_test(lf::HashMap &m)
{
    int res = 0;
    lf::Pins *pins = m.get_pins();

    res = m.insert(pins, values[0].data());
    assert(res == 0);
    res = m.insert(pins, values[0].data());
    //assert(res == 1);

    //std::cout << "patch1\n";
    void *d1 = m.search(pins, values[0].data(), 4);
    assert(d1);
    //std::cout << std::string((const char *)d1, 7) << std::endl;
    m.search_unpin(pins);

    res = m.remove(pins, values[0].data(), 4);
    assert(res == 0);
    // not found
    res = m.remove(pins, values[0].data(), 4);
    assert(res == 1);

    res = m.insert(pins, values[0].data());
    assert(res == 0);
    res = m.remove(pins, values[0].data(), 4);
    assert(res == 0);

    m.put_pins(pins);
}

void single_thread_base_test(int loops)
{
    lf::HashMap m(7, LF_HASH_UNIQUE, 0, 4, nullptr);

    for (int i = 0; i < loops; i++)
    {
        base_test(m);
    }
}

void thd_enter(lf::HashMap *m, int from, int cnt)
{
    int res;
    lf::Pins *pins = m->get_pins();

    int64_t value;
    for (int i = 0; i < cnt; i++)
    {
        value = from + i;
        value = value << 32 | ((int64_t)from);
        res = m->insert(pins, &value);
    }

    m->put_pins(pins);
    (void) res;
}

void multi_thread_base_test(int thdno)
{
    int keylen = sizeof(int32_t);
    lf::HashMap m(keylen * 2, LF_HASH_UNIQUE, 0, keylen, nullptr);
    std::vector<std::thread *> thds;

    for (int i = 0; i < thdno; i++)
    {
        std::thread *thd = new std::thread(std::bind(&thd_enter, &m, i * 100000 - 5000, 100000));
        thds.push_back(thd);
    }

    for (int i = 0; i < thdno; i++)
    {
        thds[i]->join();
        delete thds[i];
    }
}

int main(int argc, char *argv[])
{
    //single_thread_base_test(10000000);

    multi_thread_base_test(5);

    return 0;
}
