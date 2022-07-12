#pragma once
#include <stdint.h>
#include <atomic>
template <typename T>
struct LockFreeFIFOLink
{
    T data;
    int32_t next;
};

template<typename T, int MAX_LEN>
struct LockFreeFIFO
{
    typedef union {
        struct {
            int32_t head;
            int32_t aba;
        };
        uint64_t combined;
    } ABAIndex;

    ABAIndex fifo;
    ABAIndex free;

    void push_fifo(int index);
    int pop_fifo();
    void push_free(int index);
    int pop_free();
    int steal_fifo();
    int steal_free();

    LockFreeFIFOLink<T> all_links[MAX_LEN];
    struct LockFreeFIFO();
};