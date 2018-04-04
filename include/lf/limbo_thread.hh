#pragma once

/*
1. 需要维护两个全局的epoch， latest_epoch, min_active_epoch
latest_epoch 在内存释放时，用于标识释放的时间点
min_active_epoch 是活跃的thread中最小的epoch。
内存在释放点小于min_active_epoch再可以被释放。
2. 固定住工作线程的数量，每个线程对应一个limbo_thread对象。
limbo_thread对象维护自己的active_epoch, limbo_group链表。
3. 因为事务的跨度完全由用户定义，所以不能假定一个线程只能线性的处理一个事务。
真实情况应该是n个工作线程处理m个事务（m>n），单个事务可以在多个线程上线性调度。
所以在线程上维护min_active_epoch变的非常不适合。
*/
namespace lf {
    ;
} // end namespace 
