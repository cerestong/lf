#pragma once
#include "lf/dyn_array.hh"

namespace lf
{

/*
    Allocator实现了一个可以分配固定大小的分配器，
    并且优先重用内存池的内存，过程中无锁
    */

enum
{
    PINBOX_PINS = 4,
    PURGATORY_SIZE = 10
};

struct Pins;
typedef void(PinBoxFreeFunc)(void *, void *, void *);
class PinBox
{
  public:
    PinBox(uint32_t free_ptr_offset,
           PinBoxFreeFunc *free_func,
           void *free_func_arg);
    ~PinBox() {}

    Pins *get_pins();

    DynArray *pinarray() { return &pinarray_; }

  public:
    DynArray pinarray_;
    PinBoxFreeFunc *free_func_;
    void *free_arg_;
    uint32_t free_ptr_offset_;
    uint32_t volatile pinstack_top_ver_; // this is a versioned pointer
    uint32_t volatile pins_in_array_;    // number of elements in array
};

/*
        pod object
    */
struct Pins
{
    void *volatile pin[PINBOX_PINS];
    PinBox *pinbox;
    void *purgatory;
    uint32_t purgatory_count;
    uint32_t volatile link;
    // want sizeof(Pins) to be 64 avoid false sharing
    char pad[64 - sizeof(uint32_t) * 2 - sizeof(void *) * (PINBOX_PINS + 2)];
};

static inline void pin(struct Pins *pins, int pin, void *addr)
{
    atomic_storeptr(&(pins->pin[pin]), addr);
}
static inline void unpin(struct Pins *pins, int pin)
{
    atomic_storeptr(&(pins->pin[pin]), nullptr);
}

void put_pins(struct Pins *pins);
void pins_free(struct Pins *pins, void *addr);

/*
        memory allocator
    */
typedef void(allocator_func)(uint8_t *);

/*
        Allocator的ctor和dtor参数谨慎设置：内存块malloc后会调用ctor，
        在内存块free前调用dtor。
        注意alloc返回缓存池中的数据并不会调用ctor
    */
class Allocator
{
  public:
    PinBox pinbox_;
    uint8_t *volatile top_;
    uint32_t element_size_;
    uint32_t volatile mallocs_;
    // called, when an object is malloc()'ed
    allocator_func *constructor_;
    // called, when an oject is free()'ed
    allocator_func *destructor_;

  public:
    Allocator(uint32_t size, uint32_t free_ptr_offset,
              allocator_func *ctor, allocator_func *dtor);
    Allocator(uint32_t size, uint32_t free_ptr_offset);
    ~Allocator();

    uint32_t pool_count();

    Pins *get_pins()
    {
        return pinbox_.get_pins();
    }

    void direct_free(void *addr)
    {
        if (destructor_)
            destructor_((uint8_t *)addr);
        free(addr);
    }

    static void *alloc(Pins *pins);
};
} // end namespace
