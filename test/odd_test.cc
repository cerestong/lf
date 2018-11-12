#include "lf/logger.hh"
#include <stddef.h>

int32_t rough_log2(int64_t input)
{
    int32_t c = 0;
    if (input >= 0)
    {
        while ((input >> c) && (c < (int32_t)(sizeof(int64_t) * 8)))
            c++;
    }
    else
    {
        while (((input >> c) < -1) && (c < (int32_t)(sizeof(int64_t) * 8)))
            c++;
    }
    return c;
}

void rough_log_test()
{
    lf::log("rough_log2(1) %d", rough_log2(1));
    lf::log("rough_log2(10) %d", rough_log2(10));    
    lf::log("rough_log2(100) %d", rough_log2(100));    
    lf::log("rough_log2(1001) %d", rough_log2(1001));    
    lf::log("rough_log2(11111) %d", rough_log2(11111));    

    lf::log("rough_log2(-1) %d", rough_log2(-1));
    lf::log("rough_log2(-10) %d", rough_log2(-10));    
    lf::log("rough_log2(-100) %d", rough_log2(-100));    
    lf::log("rough_log2(-1001) %d", rough_log2(-1001));    
    lf::log("rough_log2(-11111) %d", rough_log2(-11111));
}

struct RepType
{
    const char *data;
    int length;
    int memo_offset;
};

template <typename T, size_t S = sizeof(RepType) - sizeof(T)>
struct RepItem;

template <typename T>
struct RepItem<T, 4>
{
    T x;
    int type;
};

template <typename T>
struct RepItem<T, 8>
{
    T x;
    int padding;
    int type;
};

void union_test()
{
    lf::log("sizeof(RepType) %d", sizeof(RepType));
    lf::log("sizeof(RepItem<uint64_t>) %d", sizeof(RepItem<uint64_t>));
    lf::log("sizeof(RepItem<double>) %d", sizeof(RepItem<double>));

    union is {
        uint64_t i;
        // 带string的union是定义非法的，编译器会报错。
        //union不允许有构造和析构函数
        //std::string s;
        char a[32];
    } u;

    u.i = 123;
}

void align_test()
{
    struct S {
        char a;
        char b;
    };
    struct X {
        int n;
        char c;
    };
    struct Y {
        int n;
        alignas(64) char c;
    };
    lf::log("sizeof(S) = %d, alignof(S) = %d", sizeof(S), alignof(S));
    lf::log("sizeof(X) = %d, alignof(X) = %d", sizeof(X), alignof(X));
    lf::log("sizeof(Y) = %d, alignof(Y) = %d", sizeof(Y), alignof(Y));
    lf::log("alignof(std::max_align_t) = %d", alignof(max_align_t));
}

int main(int argc, char **argv)
{
    lf::g_stdout_logger_on = true;

    rough_log_test();
    lf::log("-10 %x", -10);
    lf::log("-10 >> 1 %x", -10 >> 1);   

    lf::log("sizeof(intptr_t) %d", sizeof(intptr_t)); 
        
    union_test();
    align_test();

    return 0;
}
