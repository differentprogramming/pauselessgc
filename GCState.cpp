#include <iostream>
#include "Collectable.h"
#include <cassert>
#ifdef _WIN32
#include <Processthreadsapi.h>
#else
#include <sched.h>
#endif

/*
Phase diagram
I

1)        NOT_COLLECTING, [double store write barrier]
2)        Border between NOT_COLLECTING and COLLECTING while the GC and threads are waiting for every thread to count out of NOT_COLLECTING
3)        COLLECTING, [single store write barrier]
4)        Border between COLLECTING and RESTORING_SNAPSHOT while the GC and threads are waiting for every thread to count out of COLLECTING
1)        RESTORING_SNAPSHOT, [double store write barrier]
        border between RESTORING_SNAPSHOT and NOT_COLLECTING doesn't require any acknowledgement - it only means that a new collection phase can start.
        therefore we don't need a threads_acknowledged_snapshot counter, threads can go straight to counting into threads_out_of_collection
        it's ok for work threads to go straight from RESTORING_SNAPSHOT to border of NOT_COLLECTING and COLLECTING
A) not mutating        

A) -> 1)
A) -> 2)
A) -> 3)
A) -> 4)
1) -> A)
2) -> A)
3) -> A)
4) -> A)

II
if a thread is both a mutator and the main gc thread, then:
safe points are just null functions
you need a different version of gc stage functions that also change the mutator state
A) -> border states 2,4 can't happen
2,4 to A) can't happen

III
if gc happens inside of allocation then the easiest thing is to transition to not-mutating and do the GC

IV 
if multiple GC threads are wanted then GC work has to be broken up and distributed through atomic FIFOs
GC transitions happen:
1) when there is no work left
2) one of the GC threads grabs a mutex over the state and runs a transition
*/

using namespace neosmart;

namespace GC {



    std::atomic_bool ThreadSlots[MAX_COLLECTED_THREADS];

    StateStoreType State;

    int64_t MaxTriggerPoint;
    std::atomic_int64_t TriggerPoint;
    std::atomic_int64_t Allocated;
    thread_local int64_t ThreadAllocated;
    thread_local int AggregateLogAlloc;
    thread_local int AggregateArrayLogAlloc;


    thread_local void (*write_barrier)(SnapPtr*, void*);

    thread_local PhaseEnum ThreadState;
    thread_local int NotMutatingCount;
    thread_local int MyThreadNumber;
    thread_local bool CombinedThread;
    neosmart_event_t StartCollectionEvent;
    std::thread CollectionThread;

    void log_alloc(size_t a)
    {
        ThreadAllocated += a;
        if (++AggregateLogAlloc > 300) {
            AggregateLogAlloc = 0;
            Allocated += ThreadAllocated;
            ThreadAllocated = 0;
            if (Allocated > TriggerPoint) {
                if (Allocated.exchange(0) > TriggerPoint) SetEvent(StartCollectionEvent);
            }
        }
    }
    void log_array_alloc(size_t a, size_t n)
    {
        ThreadAllocated += a + n;
        if (++AggregateArrayLogAlloc > 20) {
            AggregateArrayLogAlloc = 0;
            Allocated += ThreadAllocated;
            ThreadAllocated = 0;
            if (Allocated > TriggerPoint) {
                if (Allocated.exchange(0) > TriggerPoint) SetEvent(StartCollectionEvent);
            }
        }
    }


    void regular_write_barrier(SnapPtr* dest, void* v) {
        assert(ThreadState != PhaseEnum::NOT_MUTATING);
        double_ptr_store(dest, v);
    }
    void collecting_write_barrier(SnapPtr* dest, void* v) {
        assert(ThreadState != PhaseEnum::NOT_MUTATING);
        single_ptr_store(dest, v);
    }

    void SetThreadState(PhaseEnum v) {
        ThreadState = v;
        if (v == PhaseEnum::COLLECTING) {
            write_barrier = collecting_write_barrier;
        }
        else write_barrier = regular_write_barrier;
    }


    std::atomic_uint32_t ThreadsInGC;

    void merge_collected()
    {
        /*
    extern Collectable* ActiveCollectables[MAX_COLLECTED_THREADS*2];
    extern thread_local RootLetterBase* ActiveRoots[MAX_COLLECTED_THREADS*2];
    extern int ActiveIndex;
    */
        for (int i = 0; i < MAX_COLLECTED_THREADS; ++i) {
            if (nullptr == ScanListsByThread[i]) continue;
            Collectable* active_c = ScanListsByThread[i]->collectables[ActiveIndex];
            Collectable* snapshot_c = ScanListsByThread[i]->collectables[(ActiveIndex^1)];
            if (snapshot_c->sweep_next != snapshot_c)
            {
                /*
                
        1sp <-   2s   -> 3sn            1sp >< 6an .. 4ap >< 5a >< 3sn .. 1sp
            ->6an    5a<-
        4ap ->   5a  ->3sn 6an               
            <-     1sp<-
                */
                snapshot_c->sweep_next->sweep_prev = active_c;
                snapshot_c->sweep_prev->sweep_next = active_c->sweep_next;
                active_c->sweep_next->sweep_prev = snapshot_c->sweep_prev;
                active_c->sweep_next = snapshot_c->sweep_next;
                snapshot_c->sweep_next = snapshot_c->sweep_prev = snapshot_c;

            }
            //save the start before any new allocations
            ScanListsByThread[i]->collectables[2]= ScanListsByThread[i]->collectables[ActiveIndex]->sweep_next;
            
            RootLetterBase* active_r = ScanListsByThread[i]->roots[ActiveIndex];
            RootLetterBase* snapshot_r = ScanListsByThread[i]->roots[(ActiveIndex ^ 1)];
            if (snapshot_r->next != snapshot_r)
            {
                snapshot_r->next->prev = active_r;
                snapshot_r->prev->next = active_r->next;
                active_r->next->prev = snapshot_r->prev;
                active_r->next = snapshot_r->next;
                snapshot_r->next = snapshot_r->prev = snapshot_r;

            }
            ScanListsByThread[i]->roots[2] = ScanListsByThread[i]->roots[ActiveIndex]->next;
        }
 
    }

    void collect_thread();

    void init()
    {
        State.state.threads_not_mutating = 0;
        State.state.threads_in_sweep = 0;
        State.state.threads_out_of_collection = 0;
        State.state.threads_in_collection = 0;
        ActiveIndex = 0;
        State.state.phase = PhaseEnum::NOT_COLLECTING;
        ThreadsInGC.store(0, std::memory_order_seq_cst);
        for (int i = 0; i < MAX_COLLECTED_THREADS; ++i) {
            ScanListsByThread[i] = nullptr;
            ThreadSlots[i] = false;
        }
        StartCollectionEvent = CreateEvent();
        CollectionThread = std::thread(collect_thread);
        TriggerPoint = 100000000;
    }

    /*
    if ( nBlockUse == _CRT_BLOCK )
    return( TRUE );
    
    int YourAllocHook(int nAllocType, void *pvData,
        size_t nSize, int nBlockUse, long lRequest,
        const unsigned char * szFileName, int nLine )

        //nAllocType _HOOK_ALLOC, _HOOK_REALLOC, or _HOOK_FREE

        return true
    
    */
    void _do_collection() 
    {
        //mark
        for (int i = 0; i < MAX_COLLECTED_THREADS; ++i) {
            if (nullptr == ScanListsByThread[i]) continue;
            RootLetterBase* snapshot_r = ScanListsByThread[i]->roots[(ActiveIndex ^ 1)];
            RootLetterBase* n = snapshot_r->next;
            while (n != snapshot_r) {
                n->mark();
                n = n->next;
            }

        }
        //sweep
        for (int i = 0; i < MAX_COLLECTED_THREADS; ++i) {
            if (nullptr == ScanListsByThread[i]) continue;
            Collectable* snapshot_c = ScanListsByThread[i]->collectables[(ActiveIndex ^ 1)];
            Collectable* c = snapshot_c->sweep_next;
            while (c != snapshot_c) {
                Collectable* t = c;
                c = c->sweep_next;
                if (!t->marked) delete t;
            }

            RootLetterBase* snapshot_r = ScanListsByThread[i]->roots[(ActiveIndex ^ 1)];
            RootLetterBase* n = snapshot_r->next;
            while (n != snapshot_r) {
                RootLetterBase* t = n;
                n = n->next;
                if (!t->owned) delete t;
            }
        }
    }


    void _do_restore_snapshot()
    {

        for (int i = 0; i < MAX_COLLECTED_THREADS; ++i) {
            if (nullptr == ScanListsByThread[i]) continue;
            Collectable* snapshot_c = ScanListsByThread[i]->collectables[ActiveIndex];
            Collectable* t = ScanListsByThread[i]->collectables[2];
            while (t != snapshot_c) {
                t = t->sweep_next;
                for (int j = t->total_instance_vars() - 1; j >= 0; --j) {
                    restore(&t->index_into_instance_vars(j)->value);
                }
            }            
            RootLetterBase* snapshot_r = ScanListsByThread[i]->roots[ActiveIndex];
            RootLetterBase* r = ScanListsByThread[i]->roots[2];
            while (r != snapshot_r) {
                restore(r->double_ptr());
                r = r->next;
            }
        }
    }
    void _do_finalize_snapshot()
    {
        return;
        std::cout << "actually about to finalize snapshot \n";
        for (int i = 0; i < MAX_COLLECTED_THREADS; ++i) {
            if (nullptr == ScanListsByThread[i]) continue;
            Collectable* snapshot_c = ScanListsByThread[i]->collectables[ActiveIndex];
            Collectable* t = ScanListsByThread[i]->collectables[2];
            while (t != snapshot_c) {
                t = t->sweep_next;
                for (int j = t->total_instance_vars() - 1; j >= 0; --j) {
                    restore(&t->index_into_instance_vars(j)->value);
                }
            }
            RootLetterBase* snapshot_r = ScanListsByThread[i]->roots[ActiveIndex];
            RootLetterBase* r = ScanListsByThread[i]->roots[2];
            while (r != snapshot_r) {
                restore(r->double_ptr());
                r = r->next;
            }

        }
    }

    void _start_collection()
    {
        StateStoreType gc = get_state();
        assert(gc.state.phase == PhaseEnum::NOT_COLLECTING);

        bool one_shot = false;
        std::cout << "threads_out_of_collection start " << (int)gc.state.threads_out_of_collection << "\nthreads_in_collection starts " << (int)gc.state.threads_in_collection << "\n";
        StateStoreType to;
        do {
            to = gc;
            to.state.phase = PhaseEnum::COLLECTING;
            to.state.threads_out_of_collection++;//stop everyone till I'm done
        } while(!compare_set_state(&gc, to));

        while (true) {
            if (!one_shot && to.state.threads_in_collection != 0) {
                one_shot = true;
                std::cout << "threads_out_of_collection now " << (int)gc.state.threads_out_of_collection << "\nthreads_in_collection now " << (int)gc.state.threads_in_collection << "\n";
            }
            if (to.state.threads_out_of_collection == 1) {
                ActiveIndex ^= 1;
                do {
                    to = gc;
                    to.state.threads_out_of_collection--;//release them
                } while (!compare_set_state(&gc, to));
                if (to.state.threads_out_of_collection == 0) break;
            }
#ifdef _WIN32
            SwitchToThread();
#else
            sched_yield();
#endif 
            to = get_state();
        }
        if (CombinedThread && ThreadState !=PhaseEnum::NOT_MUTATING)  SetThreadState(PhaseEnum::COLLECTING);
        _do_collection();
    }
    //waits until no threads are collecting
    void _end_collection_start_restore_snapshot()
    {
        StateStoreType gc = get_state();
        assert(gc.state.phase == PhaseEnum::COLLECTING);
        StateStoreType to;
        Collectable* t=nullptr;
        RootLetterBase* r = nullptr;
        do {
            to = gc;
            to.state.threads_in_collection++;//stop everyone till I'm done
            to.state.phase = PhaseEnum::RESTORING_SNAPSHOT;
        } while (!compare_set_state(&gc, to));
        while (true) {
            if (to.state.threads_in_collection == 1) {
                merge_collected();
 
                do {
                    to = gc;
                    to.state.threads_in_collection--;//release them
                } while (!compare_set_state(&gc, to));
                if (to.state.threads_in_collection == 0) break;
            }
#ifdef _WIN32
            SwitchToThread();
#else
            sched_yield();
#endif 
            to = get_state();
        }
        if (CombinedThread && ThreadState != PhaseEnum::NOT_MUTATING)  SetThreadState(PhaseEnum::RESTORING_SNAPSHOT);
        _do_restore_snapshot();
        return;
    }

    void _end_sweep()
    {
        StateStoreType gc = get_state();
        assert(gc.state.phase == PhaseEnum::RESTORING_SNAPSHOT);
        StateStoreType to;
        do {
            to = gc;
            to.state.phase = PhaseEnum::NOT_COLLECTING;
            to.state.threads_in_sweep++;//stop everyone till I'm done
        } while (!compare_set_state(&gc, to));

        while (true) {
            if (to.state.threads_in_sweep == 1) {
                ActiveIndex ^= 1;
                do {
                    to = gc;
                    to.state.threads_in_sweep--;//release them
                } while (!compare_set_state(&gc, to));
                if (to.state.threads_in_sweep == 0) break;
            }
#ifdef _WIN32
            SwitchToThread();
#else
            sched_yield();
#endif 
            to = get_state();
        }
        if (CombinedThread && ThreadState != PhaseEnum::NOT_MUTATING)  SetThreadState(PhaseEnum::NOT_COLLECTING);
        _do_finalize_snapshot();

    }

    StateStoreType get_state()
    {
        StateStoreType ret;
        ret.store = ((AtomicGCStateWhole*)&State.store)->load(std::memory_order_seq_cst);
        return ret;
    }

    bool compare_set_state(StateStoreType* expected, StateStoreType to)
    {
        return std::atomic_compare_exchange_weak(((AtomicGCStateWhole*)&State.store), &expected->store, to.store);
    }

    //turns out that hazard pointers won't work because we would need a fence to make sure they're visible when we start collecting, and if we need a fence
    //then the collector has to wait for the fences, and if it's waiting for the fences it can just wait for all threads to be IN_COLLECTION and not need the hazard
    //
    //count into collection to start gc or count out of collection to start sweep
    //
    void safe_point()
    {
        if (CombinedThread) return;
        StateStoreType gc = get_state();
        StateStoreType to;
        if (ThreadState == gc.state.phase) return;
        switch (ThreadState)
        {
        case PhaseEnum::NOT_MUTATING:
            return;
        case PhaseEnum::COLLECTING:
        {
            bool success = false;
            do {
                to.state = gc.state;
                to.state.threads_in_collection--;
                to.state.threads_in_sweep++;

                success = compare_set_state(&gc, to);
            } while (!success);
            SetThreadState(PhaseEnum::RESTORING_SNAPSHOT);
            while (to.state.threads_in_collection > 0) {
#ifdef _WIN32
                SwitchToThread();
#else
                sched_yield();
#endif 
                to = get_state();
            }
            return;
        }
        case PhaseEnum::RESTORING_SNAPSHOT:
        {
            bool success = false;
            do {
                to.state = gc.state;
                to.state.threads_in_sweep--;
                to.state.threads_out_of_collection++;

                success = compare_set_state(&gc, to);
            } while (!success);
            SetThreadState(PhaseEnum::NOT_COLLECTING);
            while (to.state.threads_in_sweep > 0) {
#ifdef _WIN32
                SwitchToThread();
#else
                sched_yield();
#endif 
                to = get_state();
            }
            return;
        }
        case PhaseEnum::NOT_COLLECTING:
        {
            bool success = false;
            do {
                to.state = gc.state;
                to.state.threads_in_collection++;
                to.state.threads_out_of_collection--;
                success = compare_set_state(&gc, to);
            } while (!success);
            SetThreadState(PhaseEnum::COLLECTING);
            while (to.state.threads_out_of_collection > 0) {
#ifdef _WIN32
                SwitchToThread();
#else
                sched_yield();
#endif 
                to = get_state();
            }
            break;
        }
        }
    }

    void init_thread()
    {
        MyThreadNumber = -1;
        do {
            for (int i = 0; i < MAX_COLLECTED_THREADS; ++i) {
                bool expected = false;
                if (ThreadSlots[i] == false && ThreadSlots[i].compare_exchange_strong(expected, true)) {
                    MyThreadNumber = i;
                    break;
                }
            }
            if (MyThreadNumber == -1){
#ifdef _WIN32
                SwitchToThread();
#else
                sched_yield();
#endif         
            }
        } while (MyThreadNumber == -1);

        ThreadsInGC++;
        if (ScanListsByThread[MyThreadNumber] == nullptr) {
            ScanLists* s = new ScanLists;

            for (int i = 0; i < 2; ++i) {
                s->collectables[i] = new CollectableSentinal();
                s->roots[i] = new RootLetterBase(RootLetterBase::SENTINAL);
            }
            ScanListsByThread[MyThreadNumber] = s;
        }
        CombinedThread = false;

        NotMutatingCount = 1;
        thread_enter_mutation(true);
    }

    void exit_thread()
    {
        bool success = false;
        StateStoreType gc = get_state();
        do {
            StateStoreType to;
            to.state = gc.state;
            if (ThreadState == PhaseEnum::COLLECTING) {
                to.state.threads_in_collection--;
            }
            else if (ThreadState == PhaseEnum::NOT_COLLECTING)
            {
                to.state.threads_out_of_collection--;
            }
            else {
                to.state.threads_not_mutating--;
            }

            success = compare_set_state(&gc, to);
        } while (!success);
        ThreadSlots[MyThreadNumber] = false;
        ThreadsInGC--;
    }

    struct ThreadGCRAII
    {
        ThreadGCRAII() { init_thread(); }
        ~ThreadGCRAII() { exit_thread(); }

    };


    //if syncing packages up object and root for collecting then it has to still happen even if a thread has opted out
    //
    //clearly the lists for both of these have to be visible to the GC without having to be explicitly passed.
    //And handling the difference between live and snapshot lists has to be done entirely by the GC.
    // 
    //
    void thread_leave_mutation()
    {
        ++NotMutatingCount;
        if (NotMutatingCount > 1) {
            return;
        }
        bool success = false;
        StateStoreType gc = get_state();
        do {
            StateStoreType to;
            to.state = gc.state;
            switch (gc.state.phase) {
            case  PhaseEnum::NOT_COLLECTING:
                --to.state.threads_out_of_collection;
                break;
            case  PhaseEnum::COLLECTING:
                --to.state.threads_in_collection;
                break;
            case  PhaseEnum::RESTORING_SNAPSHOT:
                --to.state.threads_in_sweep;
            }
                
            ++to.state.threads_not_mutating;
            success = compare_set_state(&gc, to);
        } while (!success);
        SetThreadState(PhaseEnum::NOT_MUTATING);
    }
    void thread_enter_mutation(bool from_init_thread)
    {
        --NotMutatingCount;
        if (NotMutatingCount != 0) {
            return;
        }
        bool success = false;
        StateStoreType to;
        StateStoreType gc = get_state();
        do {
            to.state = gc.state;
            switch (gc.state.phase) {
            case  PhaseEnum::NOT_COLLECTING:
                ++to.state.threads_out_of_collection;
                break;
            case  PhaseEnum::COLLECTING:
                ++to.state.threads_in_collection;
                break;
            case  PhaseEnum::RESTORING_SNAPSHOT:
                ++to.state.threads_in_sweep;
            }

            if (!from_init_thread) --to.state.threads_not_mutating;
            success = compare_set_state(&gc, to);
        } while (!success);
        SetThreadState(to.state.phase);
        switch (to.state.phase)
        {
        case  PhaseEnum::NOT_COLLECTING:
            while (to.state.threads_in_sweep > 0) {
#ifdef _WIN32
                SwitchToThread();
#else
                sched_yield();
#endif 
                to = get_state();
            }
            break;
        case  PhaseEnum::COLLECTING:
            while (to.state.threads_out_of_collection > 0) {
#ifdef _WIN32
                SwitchToThread();
#else
                sched_yield();
#endif 
                to = get_state();
            }
            break;
        case  PhaseEnum::RESTORING_SNAPSHOT:
            while (to.state.threads_in_collection > 0) {
#ifdef _WIN32
                SwitchToThread();
#else
                sched_yield();
#endif 
                to = get_state();
            }
        }
    }


    void collect_thread()
    {
        for (;;) {
            if (0 != WaitForEvent(StartCollectionEvent)) return; //there was an error, get out of here
            std::cout << "starting collection\n";
            //if (TriggerPoint * 2 < MaxTriggerPoint) TriggerPoint.store(TriggerPoint*2,std::memory_order_release);
            _start_collection();
            std::cout << "starting restore snapshot\n";
            _end_collection_start_restore_snapshot();
            std::cout << "starting finalize snapshot\n";
            _end_sweep();
            std::cout << "end collection\n";
        }
    }

}