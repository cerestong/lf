#include "lf/dyn_array.hh"

/*
    Every element is aligned to sizeof(element) boundary
    (to avoid false sharing if element is big enough)
*/
namespace lf
{

static const uint64_t dynarray_idxes_in_prev_levels[] = {
    0,
    DYNARRAY_LEVEL_LENGTH,
    DYNARRAY_LEVEL_LENGTH *DYNARRAY_LEVEL_LENGTH + DYNARRAY_LEVEL_LENGTH,
    DYNARRAY_LEVEL_LENGTH *DYNARRAY_LEVEL_LENGTH *DYNARRAY_LEVEL_LENGTH + DYNARRAY_LEVEL_LENGTH *DYNARRAY_LEVEL_LENGTH + DYNARRAY_LEVEL_LENGTH};

static const uint64_t dynarray_idxes_in_prev_level[] = {
    0,
    DYNARRAY_LEVEL_LENGTH,
    DYNARRAY_LEVEL_LENGTH *DYNARRAY_LEVEL_LENGTH,
    DYNARRAY_LEVEL_LENGTH *DYNARRAY_LEVEL_LENGTH *DYNARRAY_LEVEL_LENGTH};

DynArray::DynArray(uint32_t element_size)
{
    memset((void *)level_, 0, sizeof(level_));
    size_of_element_ = element_size;
}

static void rescursive_free(void **array, int level)
{
    if (!array)
        return;
    if (level)
    {
        for (int i = 0; i < DYNARRAY_LEVEL_LENGTH; i++)
        {
            rescursive_free((void **)(array[i]), level - 1);
        }
        free(array);
    }
    else
    {
        free(array[-1]);
    }
}

DynArray::~DynArray()
{
    for (int i = 0; i < DYNARRAY_LEVELS; i++)
    {
        rescursive_free((void **)(level_[i]), i);
    }
}

/*
    * return a valid lvalue pointer to the element number 'idx'
    */
void *DynArray::lvalue(uint32_t idx)
{
    void *ptr = nullptr;
    void *volatile *ptr_ptr = nullptr;
    int i;

    for (i = DYNARRAY_LEVELS - 1;
         idx < dynarray_idxes_in_prev_levels[i]; i--)
    {
        ; /*no-op*/
    }

    ptr_ptr = &level_[i];
    idx -= dynarray_idxes_in_prev_levels[i];
    for (; i > 0; i--)
    {
        if (!(ptr = *ptr_ptr))
        {
            void *newarray = malloc(DYNARRAY_LEVEL_LENGTH * sizeof(void *));
            if (unlikely(!newarray))
                return nullptr;
            else
                memset(newarray, 0, DYNARRAY_LEVEL_LENGTH * sizeof(void *));
            if (atomic_casptr(ptr_ptr, &ptr, newarray))
                ptr = newarray;
            else
                free(newarray);
        }
        ptr_ptr = ((void **)ptr) + idx / dynarray_idxes_in_prev_level[i];
        idx %= dynarray_idxes_in_prev_level[i];
    }
    if (!(ptr = *ptr_ptr))
    {
        uint8_t *newarray, *data;
        size_t len = DYNARRAY_LEVEL_LENGTH * size_of_element_ +
                     std::max<size_t>(size_of_element_, sizeof(void *));
        newarray = (uint8_t *)malloc(len);
        if (unlikely(!newarray))
            return nullptr;

        memset(newarray, 0, len);
        // reserve the space for free() address
        data = newarray + sizeof(void *);
        {
            // alignment
            intptr_t mod = ((intptr_t)data) % size_of_element_;
            if (mod)
                data += size_of_element_ - mod;
        }
        ((void **)data)[-1] = newarray; // free() will need the original pointer
        if (atomic_casptr(ptr_ptr, &ptr, data))
            ptr = data;
        else
            free(newarray);
    }
    return ((uint8_t *)ptr) + size_of_element_ * idx;
}

/*
        returns a pointer to the element number 'idx'
        or NULL if an element does not exists
    */
void *DynArray::value(uint32_t idx)
{
    void *ptr;
    void *volatile *ptr_ptr = nullptr;
    int i;

    for (i = DYNARRAY_LEVELS - 1; idx < dynarray_idxes_in_prev_levels[i]; i--)
    {
        /*no-op*/
    }
    ptr_ptr = &level_[i];
    idx -= dynarray_idxes_in_prev_levels[i];
    for (; i > 0; i--)
    {
        if (!(ptr = *ptr_ptr))
            return nullptr;
        ptr_ptr = ((void **)ptr) + idx / dynarray_idxes_in_prev_level[i];
        idx %= dynarray_idxes_in_prev_level[i];
    }
    if (!(ptr = *ptr_ptr))
        return nullptr;
    return ((uint8_t *)ptr) + size_of_element_ * idx;
}

} // end namespace
