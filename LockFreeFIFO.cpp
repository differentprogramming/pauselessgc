#include "LockFreeFIFO.h"

template<typename T, int MAX_LEN>
LockFreeFIFO<T, MAX_LEN>::LockFreeFIFO() {
    ABAIndex head;
    head.head = -1;
    head.aba = 0;

    fifo.combined = head.combined;
    for (int i = 0; i < MAX_LEN; ++i) {
        all_links[i].next = i - 1;
    }
    head.head = MAX_LEN - 1;
    ((std::atomic_uint64_t*)&free.combined)->store(head.combined, std::memory_order_seq_cst);
}


template<typename T, int MAX_LEN>
void LockFreeFIFO<T, MAX_LEN>::push_fifo(int index)
{
    int32_t head;
    head = ((std::atomic_uint32_t*)&fifo.head)->load(std::memory_order_seq_cst);
    bool success = false;
    do {
        all_links[index].next = head.head;
        success = std::atomic_compare_exchange_weak(((std::atomic_uint32_t*)&fifo.head), &head, index);
    } while (!success);
}
template<typename T, int MAX_LEN>
int LockFreeFIFO<T, MAX_LEN>::pop_fifo()
{
    int ret;
    ABAIndex head;
    head.combined = ((std::atomic_uint64_t*)&fifo.combined)->load(std::memory_order_seq_cst);
    bool success = false;
    do {
        if (head.head == -1) return -1;
        ret = head.head;
        ABAIndex to;
        to.head = all_links[ret].next;
        to.aba = head.aba + 1;
        success = std::atomic_compare_exchange_weak(((std::atomic_uint64_t*)&fifo.combined), &head.combined, to);
    } while (!success);
    return ret;
}
template<typename T, int MAX_LEN>
int LockFreeFIFO<T, MAX_LEN>::steal_fifo()
{
    int32_t head = -1;
    head = std::atomic_exchange(((std::atomic_uint32_t*)&fifo.head), head);
    return head;
}

template<typename T, int MAX_LEN>
void LockFreeFIFO<T, MAX_LEN>::push_free(int index)
{
    int32_t head;
    head = ((std::atomic_uint32_t*)&free.head)->load(std::memory_order_seq_cst);
    bool success = false;
    do {
        all_links[index].next = head.head;
        success = std::atomic_compare_exchange_weak(((std::atomic_uint32_t*)&free.head), &head, index);
    } while (!success);
}
template<typename T, int MAX_LEN>
int LockFreeFIFO<T, MAX_LEN>::pop_free()
{
    int ret;
    ABAIndex head;
    head.combined = ((std::atomic_uint64_t*)&free.combined)->load(std::memory_order_seq_cst);
    bool success = false;
    do {
        if (head.head == -1) return -1;
        ret = head.head;
        ABAIndex to;
        to.head = all_links[ret].next;
        to.aba = head.aba + 1;
        success = std::atomic_compare_exchange_weak(((std::atomic_uint64_t*)&free.combined), &head.combined, to);
    } while (!success);
    return ret;
}
template<typename T, int MAX_LEN>
int LockFreeFIFO<T, MAX_LEN>::steal_free()
{
    int32_t head = -1;
    head = std::atomic_exchange(((std::atomic_uint32_t*)&free.head), head);
    return head;
}