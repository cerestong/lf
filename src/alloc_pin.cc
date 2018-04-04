#include "lf/alloc_pin.hh"
#include <assert.h>
#include <thread>

/*
    wait-free concurrent allocator based on pinning addresses

    it works as follows: every thread has a small array of pointers.
    they're called 'pins'. Before using an object its address must
    be stored in this array(pinned). when an object is no logger
    necessary its address must be removed from this array(unpinned).
    when a thread wants to free() an object it sans all pins of all
    threads to see if somebody has this object pinned. 
    if yes - the object is not freed(but stored in a 'purgatory').
    to reduce the cost of a single free() pins are not scanned
    on every free() but only added to(thread-local) purgatory.
    on every PURGATORY_SIZE free() purgatory is scanned and all
    unpinned objects are freed.

    Pins are used to sloved ABA problem. To use pins one must obey
    a pinning protocol:

    1. 假设PTR是一个指向一个对象的共享指针，任意线程在任意时刻都可以修改PTR使得其指向其他对象，
	并释放旧对象。然后释放的对象可以被其他线程获取并重新赋值给PTR。这就是ABA问题。
    2. 创建本地指针LOCAL_PTR.
    3. 在循环中Pin住PTR:
        do {
            LOCAL_PTR = PTR;
            pin(PTR, PIN_NUMBER);
        } while (LOCAL_PTR != PTR)
    4. 这可以保证在循环结束时，LOCAL_PTR指向一个对象（PTR为空时，可能为空），
        并且这个对象不会被删除。但是这并不一直保证 LOCAL_PTR == PTR。
    5. 当对象使用完毕后，移除pin： unpin(PIN_NUMBER)
    6. when copying pins (as in the list traversing loop:)
        pin(CUR, 1);
        while () {
            do {
                NEXT = CUR->next;
                pin(NEXT, 0);
            } while (NEXT != CUR->next);
            ...
            ...
            CUR = NEXT;
            pin(CUR, 1);  // copy pin[0] to pin[1]
        }
        这样可以使CUR的地址一直时pinned状态，注意： pins只能向上复制，pin[N] to pin[M],M>N.
    7. 不要长时间的pin住对象，pins的数量很小

    解释：
	3. 循环时必要的，例如：
		thread1> LOCAL_PTR = PTR
		thread2> free(PTR); PTR = 0
		thread1> pin(PTR, PIN_NUMBER);
		现在thread1时不能访问LOCAL_PTR的，因为这是一个已被释放的对象。
	6. 当线程准备释放一个LOCAL_PTR, 它会从下往上的遍历所有的pins查看此对象是否被pinned。
		所以其他线程以相同的顺序拷贝pin- 从下往上，否则会漏掉。

	ABA问题的解决：使用单调增的版本使得释放后重新复制的PTR与之前观察的LOCAL_PTR不同，
			避免多个观察者去多次分配

*/

namespace lf
{
enum
{
    PINBOX_MAX_PINS = 65536
};

static void pinbox_real_free(Pins *pins);

/*
        free_ptr_offset: 
            每个element中由于构建可释放element链表的next指针偏移量
        free_func(first_ele, last_ele, free_arg) 
    */
PinBox::PinBox(uint32_t free_ptr_offset,
               PinBoxFreeFunc *free_func,
               void *free_func_arg)
    : pinarray_(sizeof(Pins)),
      free_func_(free_func),
      free_arg_(free_func_arg),
      free_ptr_offset_(free_ptr_offset),
      pinstack_top_ver_(0),
      pins_in_array_(0)
{
    assert(free_ptr_offset % sizeof(void *) == 0);
    static_assert(sizeof(Pins) == 64, "sizof(Pins) != 64");
}

/*
        get a new Pins from a stack of unused pins,
        or allocate a new one out of dynarray.
    */
Pins *PinBox::get_pins()
{
    uint32_t pins, next, top_ver;
    Pins *el;

    /*
            we have an array of max. 64k elements.
            the highest index currently allocated is pins_in_array.
            freed elements are in a filo stack, pinstack_top_ver.
            pinstack_top_ver_ is 32 bits; 16 low bits are the index
            in the array,to the first element of the list. 16 high 
            bits are a version (every time the 16 low bits are updated,
            the 16 high bits are incremented). 
            Versioning prevents the ABA problem.
        */
    top_ver = pinstack_top_ver_;
    do
    {
        if (!(pins = top_ver % PINBOX_MAX_PINS))
        {
            /* the stack of free elements is empty */
            pins = atomic_add32((int32_t volatile *)&pins_in_array_, 1) + 1;
            if (unlikely(pins >= PINBOX_MAX_PINS))
                return nullptr;
            /*
                    note that the first allocated element has index 1 (pins==1).
                    index 0 is reserved to mean 'null pointer'
                */
            el = (Pins *)(pinarray_.lvalue(pins));
            if (unlikely(!el))
                return nullptr;
            break;
        }
        el = (Pins *)(pinarray_.value(pins));
        next = el->link;
    } while (!atomic_cas32((int32_t volatile *)&pinstack_top_ver_,
                           (int32_t *)&top_ver,
                           top_ver - pins + next + PINBOX_MAX_PINS));
    /*
            set el->link_ to the index of el in the dynarray:
            - if element is allocated, it's its own index
            - if element is free, it's its next element in the free stack
        */
    el->link = pins;
    el->pinbox = this;
    el->purgatory_count = 0;
    return el;
}

/*
        put pins back to a pinbox
        empty the purgatory (xxx deadlock warning below!)
        push Pins to a stack
    */
void put_pins(struct Pins *pins)
{
    uint32_t top_ver, nr;
    nr = pins->link;

    /*
            xxx this will deadlock if other threads will wait for
            the caller to do something after put_pins(),
            and they would have pinned addresses that the caller wants 
            to free.
            thus: only free pins when all work is done and nobody
            can wait for you!!!
        */
    while (pins->purgatory_count)
    {
        pinbox_real_free(pins);
        if (pins->purgatory_count)
        {
            std::this_thread::yield();
        }
    }
    top_ver = pins->pinbox->pinstack_top_ver_;
    do
    {
        pins->link = top_ver % PINBOX_MAX_PINS;
    } while (!atomic_cas32((int32_t volatile *)&pins->pinbox->pinstack_top_ver_,
                           (int32_t *)&top_ver,
                           top_ver - pins->link + nr + PINBOX_MAX_PINS));
}

/*
    get the next pointer in the purgatory list.
    note that next_node is not used to avoid the extra volatile.
*/
#define pnext_node(P, X) (*((void **)(((char *)(X)) + (P)->free_ptr_offset_)))

static inline void add_to_purgatory(Pins *pins, void *addr)
{
    pnext_node(pins->pinbox, addr) = pins->purgatory;
    pins->purgatory = addr;
    pins->purgatory_count++;
}

/*
        free an object allocated via pinbox allocator

        add an object to purgatory. if necessary, call 
        pinbox_real_free() to actually free something.
    */
void pins_free(struct Pins *pins, void *addr)
{
    add_to_purgatory(pins, addr);
    if (pins->purgatory_count % PURGATORY_SIZE == 0)
        pinbox_real_free(pins);
}

struct MatchAndSaveArg
{
    Pins *pins;
    PinBox *pinbox;
    void *old_purgatory;
};

static int match_and_save(void *parg1, void *parg2)
{
    Pins *el = (Pins *)parg1;
    struct MatchAndSaveArg *arg = (struct MatchAndSaveArg *)parg2;
    Pins *el_end = el + DYNARRAY_LEVEL_LENGTH;
    for (; el < el_end; el++)
    {
        for (int i = 0; i < PINBOX_PINS; i++)
        {
            void *p = el->pin[i];
            if (p)
            {
                /*
                        对purgatory链表中所有等于p的都串联到pins->purgatory,
                        不相等的都保留在arg链表里
                    */
                void *cur = arg->old_purgatory;
                void **list_prev = &arg->old_purgatory;
                while (cur)
                {
                    void *next = pnext_node(arg->pinbox, cur);
                    if (p == cur)
                    {
                        // pinned - keeping
                        add_to_purgatory(arg->pins, cur);
                        // unlike from old purgatory
                        *list_prev = next;
                    }
                    else
                    {
                        list_prev = (void **)((char *)cur + arg->pinbox->free_ptr_offset_);
                    }
                    cur = next;
                }
                if (!arg->old_purgatory)
                    return 1;
            }
        }
    }
    return 0;
}
/*
        scan the purgatory and free everything that can be freed
    */
static void pinbox_real_free(Pins *pins)
{
    PinBox *pinbox = pins->pinbox;

    /* store info about current purgatory */
    struct MatchAndSaveArg arg = {pins, pinbox, pins->purgatory};
    /* reset purgatory */
    pins->purgatory = nullptr;
    pins->purgatory_count = 0;

    pinbox->pinarray()->iterate(match_and_save, &arg);

    if (arg.old_purgatory)
    {
        /* some objects in the old purgatory were not pinned, free then */
        void *last = arg.old_purgatory;
        while (pnext_node(pinbox, last))
            last = pnext_node(pinbox, last);
        pinbox->free_func_(arg.old_purgatory, last, pinbox->free_arg_);
    }
}

#define next_node(P, X) (*((uint8_t * volatile *)(((uint8_t *)(X)) + (P)->free_ptr_offset_)))
#define anext_node(X) next_node(&allocator->pinbox_, (X))
/*
    lock-free memory allocator for fixed-size objects
*/
static void alloc_free(uint8_t *first,
                       uint8_t volatile *last,
                       Allocator *allocator)
{
    /*
        we need a union here to access type-punned pointer reliably.
        otherwise gcc -fstrict-aliasing will not see 'tmp' changed
        in the loop
    */
    union {
        uint8_t *node;
        void *ptr;
    } tmp;
    tmp.node = allocator->top_;
    do
    {
        next_node(&allocator->pinbox_, last) = tmp.node;
    } while (!atomic_casptr((void *volatile *)(char *)&allocator->top_,
                            (void **)&tmp.ptr,
                            first));
}

/*
    param size : a size of an object to allocate.
    param free_ptr_offset: an offset inside the object to a sizeof(void *)
                            memory that is guaranteed to be unused after
                            the object is put in the purgatory. 
                            unused by any thread, not only the purgatory owner.
                            this memory will be used to link
                            waiting-to-be-freed objects in a purgatory list.
    param ctor: function to be called after object was malloc'ed
    param dtor: function to be called before object is free'ed
*/
Allocator::Allocator(uint32_t size, uint32_t free_ptr_offset,
                     allocator_func *ctor, allocator_func *dtor)
    : pinbox_(free_ptr_offset, (PinBoxFreeFunc *)alloc_free, this),
      top_(nullptr),
      element_size_(size),
      mallocs_(0),
      constructor_(ctor),
      destructor_(dtor)
{
    assert(size >= sizeof(void *) + free_ptr_offset);
}

Allocator::Allocator(uint32_t size, uint32_t free_ptr_offset)
    : pinbox_(free_ptr_offset, (PinBoxFreeFunc *)alloc_free, this),
      top_(nullptr),
      element_size_(size),
      mallocs_(0),
      constructor_(nullptr),
      destructor_(nullptr)
{
    assert(size >= sizeof(void *) + free_ptr_offset);
}

Allocator::~Allocator()
{
    uint8_t *node = top_;
    while (node)
    {
        uint8_t *tmp = next_node(&pinbox_, node);
        if (destructor_)
        {
            destructor_(node);
        }
        free(node);
        node = tmp;
    }
    top_ = nullptr;
}

/*
    allocate and return an new object

    pop an unused object from the stack or malloc it is the stack
    is empty.
    pin[0] is used, it's removed on return.
*/
void *Allocator::alloc(Pins *pins)
{
    Allocator *allocator = (Allocator *)(pins->pinbox->free_arg_);
    uint8_t *node;
    for (;;)
    {
        do
        {
            node = allocator->top_;
            pin(pins, 0, node);
        } while (node != allocator->top_);
        if (!node)
        {
            node = (uint8_t *)malloc(allocator->element_size_);
            if (allocator->constructor_)
            {
                allocator->constructor_(node);
            }
            if (likely(node))
            {
                atomic_add32((int32_t volatile *)&allocator->mallocs_, 1);
            }
            break;
        }
        if (atomic_casptr((void *volatile *)(char *)&allocator->top_,
                          (void **)&node,
                          anext_node(node)))
        {
            break;
        }
    }
    unpin(pins, 0);
    return node;
}

/*
    this is not thread-safe!!!
*/
uint32_t Allocator::pool_count()
{
    uint32_t i;
    uint8_t *node;
    for (node = top_, i = 0; node; node = next_node(&pinbox_, node), i++)
    {
    }
    return i;
}

} // end namespace
