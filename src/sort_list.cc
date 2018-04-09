#include <thread>
#include <assert.h>
#include "lf/sort_list.hh"

namespace lf
{
namespace sortlist
{

/*
	在list中查找key，将位置信息设置在cursor中。list是按key排序的。
	return
		0 - not found
		1 - found
	Note
		cursor 中的指针在返回时由pins[0..2]pin住，返回时没有释放
*/
int lfind(Item *volatile *head, uint64_t key, Cursor *cursor, Pins *pins)
{
    uint64_t cur_key;
    intptr_t link;

retry:
    cursor->prev = (intptr_t *)head;
    do
    {
        cursor->curr = (Item *)(*cursor->prev);
        pin(pins, 1, cursor->curr);
    } while (*cursor->prev != (intptr_t)cursor->curr);
    for (;;)
    {
        if (unlikely(!cursor->curr))
        {
            return 0; // end of the list
        }
        do
        {
            link = cursor->curr->link;
            cursor->next = ptr(link);
            pin(pins, 0, cursor->next);
        } while (link != cursor->curr->link);

        cur_key = cursor->curr->key;
        if (*cursor->prev != (intptr_t)cursor->curr)
        {
            std::this_thread::yield();
            goto retry;
        }
        if (!deleted(link))
        {
            if (cur_key > key)
            {
                return 0;
            }
            if (cur_key == key)
            {
                return 1;
            }
            cursor->prev = &(cursor->curr->link);
            pin(pins, 2, cursor->curr);
        }
        else
        {
            /*
                found a deleted node - be nice, help the 
                other thread and remove this deleted node
            */
            if (atomic_casptr((void **)cursor->prev,
                              (void **)&cursor->curr,
                              cursor->next))
            {
                pins_free(pins, cursor->curr);
            }
            else
            {
                std::this_thread::yield();
                goto retry;
            }
        }

        cursor->curr = cursor->next;
        pin(pins, 1, cursor->curr);
    }
}

/*
插入node到list
	return
		0 - inserted
		not 0 - 重复key的指针（没有pinned）
	Note
		使用了pins[0..2], 返回时所有的pin都释放
*/
Item *linsert(Item *volatile *head, Item *node, Pins *pins)
{
    Cursor cursor;
    int res;

    for (;;)
    {
        if (lfind(head, node->key, &cursor, pins))
        {
            res = 0; /* duplicate found */
            break;
        }
        else
        {
            node->link = (intptr_t)cursor.curr;
            assert(node->link != (intptr_t)node);
            assert(cursor.prev != &node->link);
            if (atomic_casptr((void **)cursor.prev,
                              (void **)&cursor.curr,
                              node))
            {
                res = 1; // ok
                break;
            }
        }
    }
    unpin(pins, 0);
    unpin(pins, 1);
    unpin(pins, 2);

    /*
        cursor.curr is not pinned here and the pointer is unreliable.
    */
    return res ? 0 : cursor.curr;
}

/*
删除key指定的node
	return
		0 - ok
		1 - not found
	Note
		it uses pins[0..2], 返回时所有的pin都释放
*/
int ldelete(Item *volatile *head, uint64_t key, Pins *pins)
{
    Cursor cursor;
    int res;

    for (;;)
    {
        if (!lfind(head, key, &cursor, pins))
        {
            res = 1; /*not found*/
            break;
        }
        else
        {
            /*mark the node deleted*/
            if (atomic_casptr((void **)&(cursor.curr->link),
                              (void **)&cursor.next,
                              (void *)(((intptr_t)cursor.next) | 1)))
            {
                // and remove it from the list
                if (atomic_casptr((void **)(cursor.prev),
                                  (void **)&cursor.curr,
                                  cursor.next))
                {
                    pins_free(pins, cursor.curr);
                }
                else
                {
                    /*
                        ensure the number of "set deleted flag" actions
                        is equal to the number of "remvoe from the list" actions
                    */
                    lfind(head, key, &cursor, pins);
                }
                res = 0;
                break;
            }
        }
    }
    unpin(pins, 0);
    unpin(pins, 1);
    unpin(pins, 2);
    return res;
}

/*
查找key
	return
		0 - not found
		node - found
	Note
		it uses pins[0..2], 返回时pin[2]pin住找到的node，其他的pin都被释放
*/
Item *lsearch(Item * volatile *head, uint64_t key, Pins *pins)
{
    Cursor cursor;
    int res = lfind(head, key, &cursor, pins);
    if (res)
    {
        pin(pins, 2, cursor.curr);
    }
    unpin(pins, 0);
    unpin(pins, 1);
    return res ? cursor.curr : 0;
}

} // end namespace
} // end namespace
