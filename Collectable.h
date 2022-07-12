#pragma once
#include "GCState.h"

template <typename T>
struct Value;

template <typename T>
class InstancePtr
{
    void double_ptr_store(T* v) { GC::write_barrier(&value, (void*)v) }
public:
    GC::SnapPtr value;
    T* get() const { return (T*)GC::load(&value); }
    void store(T* v) { GC::write_barrier(&value, (void*)v) }

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
    InstancePtr(const Value<Y>& o);
    template<typename Y>
    void operator = (const Value<Y>& o);
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

    RootLetterBase(Sentinal) { next = prev = this; }
    RootLetterBase();
    virtual ~RootLetterBase()
    {

        prev->next = next;
        next->prev = prev;
    }
};

inline RootLetterBase::RootLetterBase()
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

    RootLetter() {}
    RootLetter(T *v) :value(v){}
};

template <typename T>
struct Value
{
    RootLetter<T>* var;

    void operator = (T* o)
    {
        var->value.store(o);
        return o;
    }

    template <typename Y>
    void operator = (const Value<Y>& v)
    {
        var->value.store(v.var->value.get());
        return o;
    }

    template <typename Y>
    void operator = (const InstancePtr<Y>& v)
    {
        var->value.store(v.get());
        return o;
    }

    T *get() { return var->value.get(); }
    T *operator -> () { return var->value.get(); }
    template <typename Y>
    Value(Y* v) :var(new RootLetter<T>(v)) {}
    template <typename Y>
    Value (const Value<Y>  &v) :var(new RootLetter<T>(v.var->value.get())){}
    template <typename Y>
    Value (const InstancePtr<Y> &v) :var(new RootLetter<T>(v.get())) {}
    Value() :var(new RootLetter<T>){}

};

template< class T, class U >
Value<T> static_pointer_cast(const Value<U>& v) noexcept
{
    return Value<T>(static_cast<T*>(v.var->value.get()));
}

template< class T, class U >
Value<T> const_pointer_cast(const Value<U>& v) noexcept
{
    return Value<T>(const_cast<T*>(v.var->value.get()));
}

template< class T, class U >
Value<T> const_dynamic_cast(const Value<U>& v) noexcept
{
    return Value<T>(dynamic_cast<T*>(v.var->value.get()));
}

template< class T, class U >
Value<T> const_reinterpret_cast(const Value<U>& v) noexcept
{
    return Value<T>(reinterpret_cast<T*>(v.var->value.get()));
}

template<typename T>
template<typename Y>
InstancePtr<T>::InstancePtr(const Value<Y>& v) {
    double_ptr_store(v.var->value.get());
}

template<typename T>
template<typename Y>
void InstancePtr<T>::operator = (const Value<Y>& o) {
    store(v.var->value.get());
}

class Collectable {
protected:
    friend void GC::merge_collected();

    enum Sentinal { SENTINAL };
    //when not tracing contains self index
    //when tracing points back to where we came from or 0 if that was a root
    //when in a free list points to the next free element as an unbiased index into this block
    Collectable* back_ptr;
    Collectable* sweep_next;
    Collectable* sweep_prev;
    uint32_t back_ptr_from_counter;//came from nth snapshot ptr
    std::atomic_bool marked;
    virtual ~Collectable() {}
    Collectable(Sentinal) {
        sweep_prev = sweep_next = this;
    }

public:

    virtual int num_ptrs_in_snapshot() = 0;
    virtual GC::SnapPtr* index_into_snapshot_ptrs(int num) = 0;
    //not snapshot, includes ones that could be null because they're live
    virtual int total_collectable_ptrs() = 0;
    virtual size_t my_size() = 0;
    virtual Collectable* index_into_collectable_ptrs(int num) = 0;

    virtual void Finalize() {}; //destroy when collected
    Collectable() {
        Collectable *t = GC::ScanListsByThread[GC::MyThreadNumber]->collectables[GC::ActiveIndex];
        sweep_next = t;
        sweep_prev = t->sweep_prev;
        t->sweep_prev = sweep_prev->sweep_next = this;

    }
};

class CollectableSentinal : public Collectable
{
public:
    CollectableSentinal():Collectable(SENTINAL) {}
    virtual int num_ptrs_in_snapshot() { return 0; }
    virtual GC::SnapPtr* index_into_snapshot_ptrs(int num) { return nullptr; };
    //not snapshot, includes ones that could be null because they're live
    virtual int total_collectable_ptrs() { return 0; }
    virtual size_t my_size() { return sizeof(*this); }
    virtual Collectable* index_into_collectable_ptrs(int num) { return nullptr; }
};