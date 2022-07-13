#pragma once
#include <stdint.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <assert.h>
#include <chrono>
#include <random>
#include <signal.h>

// NB: On Windows, you must include Winbase.h/Synchapi.h/Windows.h before pevents.h
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 
#include <Windows.h>
#endif
//A library to simulate Windows events on Posix
//I use this because, on Windows, the events don't need a mutex, so the calls should be more efficient than using condition variables etc - no possible pause caused by contention over the mutex.
#include "pevents/pevents.h"

#ifdef _WIN32
#define __unused__  [[maybe_unused]]
#else
#define __unused__ __attribute__((unused))
#endif

#if defined(_MSC_VER)
/* Microsoft C/C++-compatible compiler */
#include <intrin.h>

#elif defined(__x86_64__)
#include <x86intrin.h>
#endif
#include "LockFreeFIFO.h"
namespace GC {

    typedef __m128i SnapPtr;

    inline void double_ptr_store(SnapPtr* dest, void* v)
    {
        SnapPtr temp;
        temp.m128i_u64[1] = temp.m128i_u64[0] = (uint64_t)v;
        _mm_store_si128(dest, temp);
    }

    inline void single_ptr_store(SnapPtr* dest, void* v)
    {
        dest->m128i_u64[0] = (uint64_t)v;
    }
    inline void* load(SnapPtr* dest)
    {
        return (void*)dest->m128i_u64[0];
    }
    inline SnapPtr double_ptr_swap(SnapPtr* dest, SnapPtr src)
    {
        SnapPtr cmp = _mm_load_si128(dest);
        while (!_InterlockedCompareExchange128(&dest->m128i_i64[0], src.m128i_i64[1], src.m128i_i64[0], &cmp.m128i_i64[0]));
        return cmp;
    }
    inline bool double_ptr_CAS(SnapPtr* dest, SnapPtr* expected, SnapPtr src)
    {
        return _InterlockedCompareExchange128(&dest->m128i_i64[0], src.m128i_i64[1], src.m128i_i64[0], &expected->m128i_i64[0]);
    }
    void fast_restore(SnapPtr* source)
    {
        SnapPtr temp = _mm_load_si128(source);
        if (temp.m128i_i64[0] != temp.m128i_i64[1])temp.m128i_i64[1] = temp.m128i_i64[0];

    }
    void restore(SnapPtr* source)
    {
        SnapPtr temp = _mm_load_si128(source);;
        do {
            if (temp.m128i_i64[0] == temp.m128i_i64[1]) {
                return;
            }
        } while (!_InterlockedCompareExchange128(&source->m128i_i64[0], temp.m128i_i64[0], temp.m128i_i64[0], &temp.m128i_i64[0]));
    }

    extern thread_local void (*write_barrier)(SnapPtr*, void*);

    enum class PhaseEnum : std::uint8_t
    {
        NOT_COLLECTING,
        COLLECTING,
        RESTORING_SNAPSHOT,
        EXIT
    };
    struct StateType
    {
        uint8_t threads_not_mutating;
        uint8_t threads_in_collection;
        uint8_t threads_out_of_collection;
        PhaseEnum phase;
    };

    const int MAX_COLLECTED_THREADS = 256;
    const int MAX_COLLECTION_NUMBER_BITS = 5;

    typedef uint64_t GCStateWhole;
    typedef std::atomic_uint64_t AtomicGCStateWhole;

    union StateStoreType
    {
        StateType state;
        GCStateWhole store;
    };

    extern StateStoreType State;

    enum class ThreadStateEnum {
        NOT_MUTATING,
        IN_COLLECTION,
        OUT_OF_COLLECTION
    };

    extern thread_local ThreadStateEnum ThreadState;
    extern thread_local int NotMutatingCount;
    extern thread_local int MyThreadNumber;


    extern std::atomic_uint32_t ThreadsInGC;

    void init();
    void _start_collection();
    //waits until no threads are collecting
    void _end_collection_start_sweep();
    void _end_sweep();
    StateStoreType get_state();
    bool compare_set_state(StateStoreType* expected, StateStoreType to);
    void safe_point();
    void init_thread();
    void exit_thread();
    struct ThreadRAII
    {
        ThreadRAII() { init_thread(); }
        ~ThreadRAII() { exit_thread(); }
    };
    void thread_leave_mutation();
    void thread_enter_mutation();
    struct LeaveMutationRAII
    {
        LeaveMutationRAII() { thread_leave_mutation(); }
        ~LeaveMutationRAII() { thread_enter_mutation(); }
    };

}
