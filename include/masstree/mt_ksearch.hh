#pragma once

#include "masstree/kpermuter.hh"

namespace lf
{

template <typename KA, typename T>
struct KeyComparator
{
    int operator()(const KA &ka, const T &n, int p)
    {
        return n.compare_key(ka, p);
    }
};

struct KeyIndexedPosition
{
    int i;
    int p;
    inline KeyIndexedPosition(){}
    inline constexpr KeyIndexedPosition(int i_, int p_)
        : i(i_), p(p_)
    {}
};

template <typename KA, typename T, typename F>
int key_upper_bound_by(const KA &ka, const T &n, F comparator)
{
    typename KeyPermuter<T>::type perm = KeyPermuter<T>::permutation(n);
    int l = 0, r = perm.size();
    while (l < r)
    {
        int m = (l + r) >> 1;
        int mp = perm[m];
        int cmp = comparator(ka, n, mp);
        if (cmp < 0)
            r = m;
        else if (cmp == 0)
            return m + 1;
        else
            l = m + 1;
    }
    return l;
}

template <typename KA, typename T, typename F>
KeyIndexedPosition key_lower_bound_by(const KA& ka, const T& n, F comparator)
{
    typename KeyPermuter<T>::type perm = KeyPermuter<T>::permutation(n);
    int l = 0, r = perm.size();
    while (l < r)
    {
        int m = (l + r) >> 1;
        int mp = perm[m];
        int cmp = comparator(ka, n, mp);
        if (cmp < 0)
            r = m;
        else if (cmp == 0)
            return KeyIndexedPosition(m, mp);
        else
            l = m + 1;
    }
    return KeyIndexedPosition(l, -1);
}

struct KeyBoundBinary
{
    static constexpr bool is_binary = true;

    template <typename KA, typename T>
    static inline int upper(const KA &ka, const T &n)
    {
        return key_upper_bound_by(ka, n, KeyComparator<KA, T>());
    }

    template <typename KA, typename T>
    static inline KeyIndexedPosition lower(const KA& ka, const T& n)
    {
        return key_lower_bound_by(ka, n, KeyComparator<KA, T>());
    }

    template <typename KA, typename T, typename F>
    static inline KeyIndexedPosition lower_by(const KA& ka, const T& n, F comparator)
    {
        return key_lower_bound_by(ka, n, comparator);
    }
};
} // namespace lf