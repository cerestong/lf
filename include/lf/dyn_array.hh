#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include "lf/compiler.hh"

namespace lf
{

enum
{
    DYNARRAY_LEVEL_LENGTH = 256,
    DYNARRAY_LEVELS = 4
};

/*
    只能存储pod对象（不能由析构函数），因为在lvalue()中返回的是memset为0的内存块。
    支持非pod对象的缺点是：对象大小不好控制，对于稀疏数组有构建和析构的开销
    */
class DynArray
{
  public:
    DynArray(uint32_t element_size);
    ~DynArray();

    void *value(uint32_t idx);
    void *lvalue(uint32_t idx);
    template <typename F>
    int iterate(F func, void *arg);

  private:
    void *volatile level_[DYNARRAY_LEVELS];
    uint32_t size_of_element_;
};

template <typename F>
int rescursive_iterate(void *ptr, int level, F func, void *arg)
{
    int res, i;
    if (!ptr)
        return 0;
    if (!level)
    {
        return func(ptr, arg);
    }
    for (i = 0; i < DYNARRAY_LEVEL_LENGTH; i++)
    {
        if ((res = rescursive_iterate(((void **)ptr)[i], level - 1, func, arg)))
            return res;
    }
    return 0;
}

/*
        calls func(array, arg) on every array of DYNARRAY_LEVEL_LENGTH elements
        in DynArray.
    */
template <typename F>
int DynArray::iterate(F func, void *arg)
{
    int i, res;
    for (i = 0; i < DYNARRAY_LEVELS; i++)
    {
        if ((res = rescursive_iterate(level_[i], i, func, arg)))
            return res;
    }
    return 0;
}

} // end namespace
