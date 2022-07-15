#pragma once
#include "GCState.h"

template <typename T>
struct RootPtr;
class Collectable;

class InstancePtrBase
{
protected:
public:
    GC::SnapPtr value;
    Collectable* get_collectable() { return (Collectable *)GC::load_snapshot(&value); }
    void mark();
};

template <typename T>
class InstancePtr : public InstancePtrBase
{
    void double_ptr_store(T* v) { GC::write_barrier(&value, (void*)v); }
public:

    T* get() const { return (T*)GC::load(&value); }
    void store(T* v) { GC::write_barrier(&value, (void*)v); }

    T* operator -> () { return get(); }
    InstancePtr() { GC::double_ptr_store(&value, nullptr); }
    InstancePtr(T *v) { GC::double_ptr_store(&value, (void*)v); }
    template<typename Y>
    InstancePtr(const InstancePtr<Y>& o) {
        double_ptr_store(o.get());
    }
    template<typename Y>
    void operator = (const InstancePtr<Y>& o) {
        store(o.get());
    }
    template<typename Y>
    InstancePtr(const RootPtr<Y>& o);
    template<typename Y>
    void operator = (const RootPtr<Y>& o);
};

template< class T, class U >
InstancePtr<T> static_pointer_cast(const InstancePtr<U>& v) noexcept
{
    return InstancePtr<T>(static_cast<T*>(v.get()));
}

template< class T, class U >
InstancePtr<T> const_pointer_cast(const InstancePtr<U>& v) noexcept
{
    return InstancePtr<T>(const_cast<T*>(v.get()));
}
template< class T, class U >
InstancePtr<T> dynamic_pointer_cast(const InstancePtr<U>& v) noexcept
{
    return InstancePtr<T>(dynamic_cast<T*>(v.get()));
}

template< class T, class U >
InstancePtr<T> reinterpret_pointer_cast(const InstancePtr<U>& v) noexcept
{
    return InstancePtr<T>(reinterpret_cast<T*>(v.get()));
}

class Collectable;
struct RootLetterBase;
namespace GC {
    struct ScanLists
    {
        Collectable* collectables[2];
        RootLetterBase* roots[2];
    };

    extern ScanLists* ScanListsByThread[MAX_COLLECTED_THREADS];
    extern int ActiveIndex;
}


struct RootLetterBase
{
    enum Sentinal { SENTINAL };
    RootLetterBase* next;
    RootLetterBase* prev;
    bool owned;
    virtual GC::SnapPtr* double_ptr() { return nullptr; }

    RootLetterBase(Sentinal): owned(true) { next = prev = this; }
    RootLetterBase();
    virtual void mark() {}
    virtual ~RootLetterBase()
    {

        prev->next = next;
        next->prev = prev;
    }
};

inline RootLetterBase::RootLetterBase():owned(true)
{
    RootLetterBase* t = GC::ScanListsByThread[GC::MyThreadNumber]->roots[GC::ActiveIndex];
    prev = t;
    next = t->next;
    next->prev = this;
    t->next = this;
}

template <typename T>
struct RootLetter : public RootLetterBase
{
    InstancePtr<T> value;
    virtual GC::SnapPtr* double_ptr() { return &value.value; }
    virtual void mark() { value->mark(); }

    RootLetter() {}
    RootLetter(T *v) :value(v){}
};

template <typename T>
struct RootPtr
{
    RootLetter<T>* var;

    void operator = (T* o)
    {
        var->value.store(o);
    }

    template <typename Y>
    void operator = (const RootPtr<Y>& v)
    {
        var->value.store(v.var->value.get());
    }

    template <typename Y>
    void operator = (const InstancePtr<Y>& v)
    {
        var->value.store(v.get());
    }

    T *get() { return var->value.get(); }
    T *operator -> () { return var->value.get(); }
    template <typename Y>
    RootPtr(Y* v) :var(new RootLetter<T>(v)) {}
    template <typename Y>
    RootPtr (const RootPtr<Y>  &v) :var(new RootLetter<T>(v.var->value.get())){}
    template <typename Y>
    RootPtr (const InstancePtr<Y> &v) :var(new RootLetter<T>(v.get())) {}
    RootPtr() :var(new RootLetter<T>){}
    ~RootPtr() { var->owned = false; }
};

template< class T, class U >
RootPtr<T> static_pointer_cast(const RootPtr<U>& v) noexcept
{
    return RootPtr<T>(static_cast<T*>(v.var->value.get()));
}

template< class T, class U >
RootPtr<T> const_pointer_cast(const RootPtr<U>& v) noexcept
{
    return RootPtr<T>(const_cast<T*>(v.var->value.get()));
}

template< class T, class U >
RootPtr<T> const_dynamic_cast(const RootPtr<U>& v) noexcept
{
    return RootPtr<T>(dynamic_cast<T*>(v.var->value.get()));
}

template< class T, class U >
RootPtr<T> const_reinterpret_cast(const RootPtr<U>& v) noexcept
{
    return RootPtr<T>(reinterpret_cast<T*>(v.var->value.get()));
}

template<typename T>
template<typename Y>
InstancePtr<T>::InstancePtr(const RootPtr<Y>& v) {
    double_ptr_store(v.var->value.get());
}

template<typename T>
template<typename Y>
void InstancePtr<T>::operator = (const RootPtr<Y>& v) {
    store(v.var->value.get());
}

namespace GC {
    void merge_collected();
    void _do_collection();
    void _do_restore_snapshot(Collectable*, RootLetterBase*);
    void _end_collection_start_restore_snapshot();
    void _do_finalize_snapshot();
}

class Collectable {
protected:
    friend void GC::merge_collected();
    friend void GC::_do_collection();
    friend void GC::_do_restore_snapshot(Collectable*, RootLetterBase*);
    friend void GC::_end_collection_start_restore_snapshot();
    friend void GC::_do_finalize_snapshot();

    enum Sentinal { SENTINAL };
    //when not tracing contains self index
    //when tracing points back to where we came from or 0 if that was a root
    //when in a free list points to the next free element as an unbiased index into this block
    Collectable* back_ptr;
    Collectable* sweep_next;
    Collectable* sweep_prev;
    uint32_t back_ptr_from_counter;//came from nth snapshot ptr
    //std::atomic_bool marked;
    bool marked; //only one collection thread
    virtual ~Collectable() 
    {
        sweep_next->sweep_prev = sweep_prev;
        sweep_prev->sweep_next = sweep_next;
    }
    Collectable(Sentinal) {
        sweep_prev = sweep_next = this;
    }

public:
    void mark()
    {
        Collectable* n=nullptr;
        Collectable* c = this;
        if (!c->marked) {
            c->marked = true;
            int t = total_instance_vars() - 1;
            for (;;) {
            outer:
                do {
                    if (t >= 0) {
                        n = c->index_into_instance_vars(t)->get_collectable();
                        if (n != nullptr && !n->marked) {
                            n->marked = true;
                            n->back_ptr_from_counter = t;
                            n->back_ptr = c;
                            c = n;
                            t = c->total_instance_vars()-1;
                            goto outer;
                        }
                    }
                    --t;
                } while (n == nullptr);
            }
            n = c;
            c = c->back_ptr;
            n->back_ptr = nullptr;
            t = back_ptr_from_counter - 1;
            if (c == nullptr) return;
        }
    }
    //virtual int num_ptrs_in_snapshot() = 0;
    //virtual GC::SnapPtr* index_into_snapshot_ptrs(int num) = 0;
    //not snapshot, includes ones that could be null because they're live
    virtual int total_instance_vars() = 0;
    virtual size_t my_size() = 0;
    virtual InstancePtrBase* index_into_instance_vars(int num) = 0;

    Collectable() {
        Collectable* t = GC::ScanListsByThread[GC::MyThreadNumber]->collectables[GC::ActiveIndex];
        sweep_prev = t;
        sweep_next = t->sweep_next;
        sweep_next->sweep_prev = this;
        t->sweep_next = this;
    }

};

class CollectableSentinal : public Collectable
{
public:
    CollectableSentinal():Collectable(SENTINAL) {}
    virtual int total_instance_vars() { return 0; }
    //not snapshot, includes ones that could be null because they're live
    virtual size_t my_size() { return sizeof(*this); }
    virtual InstancePtrBase* index_into_instance_vars(int num) { return nullptr; }
};