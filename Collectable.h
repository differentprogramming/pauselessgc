#pragma once
#include "GCState.h"
#include <iostream>

#define ENSURE_THROW(cond, exception)	\
	do { int __afx_condVal=!!(cond); assert(__afx_condVal); if (!(__afx_condVal)){exception;} } while (false)
//#define ENSURE(cond)		ENSURE_THROW(cond, throw std::runtime_error(#cond " failed") )
#define ENSURE(cond)
enum _before_ { _BEFORE_, _END_ };
enum _after_ { _AFTER_, _START_ };
enum _sentinel_ { _SENTINEL_ };

class CircularDoubleList;

void merge_from_to(CircularDoubleList* source, CircularDoubleList* dest);
namespace GC {
    void merge_collected();
}
class CircularDoubleList 
{
    friend void merge_from_to(CircularDoubleList* source, CircularDoubleList* dest);
    friend void GC::merge_collected();
    CircularDoubleList* prev;
    CircularDoubleList* next;
    bool is_sentinel;
    uint8_t to_remove;
public:

    //doesn't conform to a standard iterator, though I suppose I could make it
    //It is a bald iterator for a circular list with a sentinal.  It is always
    //it's bidirectional, and it's always safe to delete at the iterator and then
    //shift forward or reverse... except that you should never delete the sentinal
    //execept from an already empty list.
    class iterator
    {
        CircularDoubleList* center;
        CircularDoubleList* next;
        CircularDoubleList* prev;
    public:
        iterator() {}
        iterator(CircularDoubleList& c) :center(&c), next(c.next), prev(c.prev) {}
        iterator& remove() { 
            //center->fake_delete();
            delete center; 
            return *this; 
        }
        iterator& half_remove(uint8_t col_num) {
            if (!center->sentinel() && col_num==reinterpret_cast<std::atomic_bool *>(&center->to_remove)->exchange(col_num)) {
                delete center;
            }

            return *this;
        }
        CircularDoubleList& operator*() { return *center; }
        bool operator ++() { center = next; prev = center->prev; next = center->next;  return !center->sentinel(); }

        bool operator --() { center = prev; prev = center->prev; next = center->next;  return !center->sentinel(); }

        operator bool() { return !center->sentinel(); }
        bool operator !() { return center->sentinel(); }
    };
    virtual void fake_delete() = 0;
    iterator iterate() { return iterator(*this); }
    bool sentinel() const { return is_sentinel; }

    virtual ~CircularDoubleList() { next->prev = prev; prev->next = next;}
    void disconnect() { next->prev = prev; prev->next = next; }
    CircularDoubleList(_sentinel_) :prev(this), next(this), is_sentinel(true), to_remove(0) {}
    CircularDoubleList(_before_, CircularDoubleList* e) :prev(e->prev), next(e), is_sentinel(false), to_remove(0)
    {
        next->prev = prev->next = this;
    }
    CircularDoubleList(_after_, CircularDoubleList* e) :prev(e), next(e->next),  is_sentinel(false), to_remove(0)
    {
        next->prev = prev->next = this;
    }
    
    CircularDoubleList(CircularDoubleList&&) = delete;
    CircularDoubleList() = delete;

    bool empty() { return next == prev; }
};
inline void merge_from_to(CircularDoubleList* source, CircularDoubleList* dest) {
    assert(source->sentinel());
    assert(dest->sentinel());

    if (!source->empty()) {
        source->next->prev = dest;
        source->prev->next = dest->next;
        dest->next->prev = source->prev;
        dest->next = source->next;
        source->next = source->prev = source;
    }
}

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
    void double_ptr_store(T* v) { GC::double_ptr_store(&value, (void*)v); }
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
        Collectable* collectables[3];
        RootLetterBase* roots[3];
    };

    extern ScanLists* ScanListsByThread[MAX_COLLECTED_THREADS];
    extern int ActiveIndex;
}


struct RootLetterBase : public CircularDoubleList
{
    bool owned;
    bool deleted;

    virtual GC::SnapPtr* double_ptr() { abort(); return nullptr; }
    void fake_delete() { deleted = true; disconnect(); }
    void memtest() { 
        //ENSURE(!deleted); 
        if (deleted) std::cout << '.';
    }
    RootLetterBase(RootLetterBase&&) = delete;
    RootLetterBase(_sentinel_): CircularDoubleList(_SENTINEL_),owned(true),deleted(false) {  }
    RootLetterBase();
    virtual void mark() { abort(); }
    virtual ~RootLetterBase()
    {
    }
};

inline RootLetterBase::RootLetterBase():CircularDoubleList(_START_,GC::ScanListsByThread[GC::MyThreadNumber]->roots[GC::ActiveIndex]),owned(true),deleted(false)
{
}

template <typename T>
struct RootLetter : public RootLetterBase
{
    InstancePtr<T> value;
    virtual GC::SnapPtr* double_ptr() { //memtest(); 
    return &value.value; }
    virtual void mark() { 
        //memtest();
        Collectable* c = value.get_collectable();
        if (c != nullptr) 
            c->mark(); }

    RootLetter(RootLetter&&) = delete;

    RootLetter() {}
    RootLetter(T *v) :value(v){}
};

template <typename T>
struct RootPtr
{
    RootLetter<T>* var;

    void operator = (T* o)
    {
        //var->memtest();
        var->value.store(o);
    }

    template <typename Y>
    void operator = (const RootPtr<Y>& v)
    {
        //v->memtest();
        //var->memtest();
        var->value.store(v.var->value.get());
    }

    template <typename Y>
    void operator = (const InstancePtr<Y>& v)
    {
        //v->memtest();
        //var->memtest();
        var->value.store(v.get());
    }

    T *get() {
        //var->memtest();
        return var->value.get(); }
    T *operator -> () {
        //var->memtest();
        return var->value.get(); }
    template <typename Y>
    RootPtr(Y* v) :var(new RootLetter<T>(v)) {
        GC::log_alloc(sizeof(*var));
    }

    RootPtr(RootPtr<T>&& v) = delete;

    RootPtr(const RootPtr<T>& v) :var(new RootLetter<T>(v.var->value.get())) {
        //v.var->memtest();
        //var->memtest();
        GC::log_alloc(sizeof(*var));
    }

    template <typename Y>
    RootPtr (const RootPtr<Y>  &v) :var(new RootLetter<T>(v.var->value.get())){
        //v.var->memtest();
        //var->memtest();
        GC::log_alloc(sizeof(*var));
    }
    template <typename Y>
    RootPtr (const InstancePtr<Y> &v) :var(new RootLetter<T>(v.get())) {
        //var->memtest();
        GC::log_alloc(sizeof(*var));
    }
    RootPtr() :var(new RootLetter<T>){
        GC::log_alloc(sizeof(*var));
    }
    ~RootPtr() { var->owned = false; }
};

template< class T, class U >
RootPtr<T> static_pointer_cast(const RootPtr<U>& v) noexcept
{
    //v->memtest();
    return RootPtr<T>(static_cast<T*>(v.var->value.get()));
}

template< class T, class U >
RootPtr<T> const_pointer_cast(const RootPtr<U>& v) noexcept
{
    //v->memtest();
    return RootPtr<T>(const_cast<T*>(v.var->value.get()));
}

template< class T, class U >
RootPtr<T> const_dynamic_cast(const RootPtr<U>& v) noexcept
{
    //v->memtest();
    return RootPtr<T>(dynamic_cast<T*>(v.var->value.get()));
}

template< class T, class U >
RootPtr<T> const_reinterpret_cast(const RootPtr<U>& v) noexcept
{
    //v->memtest();
    return RootPtr<T>(reinterpret_cast<T*>(v.var->value.get()));
}

template<typename T>
template<typename Y>
InstancePtr<T>::InstancePtr(const RootPtr<Y>& v) {
    //v->memtest();
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
    void _do_restore_snapshot();
    void _end_collection_start_restore_snapshot();
    void _do_finalize_snapshot();
}

#define ONE_COLLECT_THREAD

class Collectable: public CircularDoubleList {
protected:
    friend void GC::merge_collected();
    friend void GC::_do_collection();
    friend void GC::_do_restore_snapshot();
    friend void GC::_end_collection_start_restore_snapshot();
    friend void GC::_do_finalize_snapshot();
public:
    bool deleted;
protected:
    //when not tracing contains self index
    //when tracing points back to where we came from or 0 if that was a root
    //when in a free list points to the next free element as an unbiased index into this block
    Collectable* back_ptr;

    uint32_t back_ptr_from_counter;//came from nth snapshot ptr
#ifdef ONE_COLLECT_THREAD
    bool marked; //only one collection thread
#else
    std::atomic_bool marked;
#endif
    virtual ~Collectable() 
    {
 
    }
    Collectable(_sentinel_) : CircularDoubleList(_SENTINEL_), back_ptr(nullptr) , marked(true), deleted(false){
        
    }

public:
    void memtest() const { 
        //ENSURE(!deleted); 
        if (deleted) std::cout << ':';
    }
    void fake_delete() {
        if (deleted) {
            std::cout << '$';
            return;
        }
        deleted = true; disconnect();  }
    void mark()
    {
        //memtest();
        Collectable* n=nullptr;
        Collectable* c = this;
        //if (deleted) std::cout << '!';
        if (marked) return;
#ifdef ONE_COLLECT_THREAD
        marked = true;
        {
#else
        bool got_it = marked.exchange(true);
        if (!got_it) {
#endif
            int t = total_instance_vars() - 1;
            for (;;) {
                if (t >= 0) {
                    n = c->index_into_instance_vars(t)->get_collectable();
                    if (n != nullptr) {
                        if (n->deleted) std::cout << '*';

                        if (!n->marked) {
#ifdef ONE_COLLECT_THREAD
                            n->marked = true;
                            {
#else
                            got_it = marked.exchange(true);
                            if (!got_it) {
#endif
                                n->back_ptr_from_counter = t;
                                n->back_ptr = c;
                                c = n;
                                t = c->total_instance_vars() - 1;
                                continue;
                            }
                        }
                    }
                    --t;
                }
                else {
                    if (c == this) return;
                    n = c;
                    c = c->back_ptr;
                    t = n->back_ptr_from_counter - 1;
                }
            }
        }
    }
    //virtual int num_ptrs_in_snapshot() = 0;
    //virtual GC::SnapPtr* index_into_snapshot_ptrs(int num) = 0;
    //not snapshot, includes ones that could be null because they're live
    virtual int total_instance_vars() = 0;
    virtual size_t my_size() = 0;
    virtual InstancePtrBase* index_into_instance_vars(int num) = 0;
    Collectable(Collectable&&) = delete;

    Collectable() :CircularDoubleList(_START_, GC::ScanListsByThread[GC::MyThreadNumber]->collectables[GC::ActiveIndex]), back_ptr(nullptr), marked(false),deleted(false) {
        }

};

class CollectableSentinal : public Collectable
{
public:
    CollectableSentinal():Collectable(_SENTINEL_) {}
    virtual int total_instance_vars() { return 0; }
    //not snapshot, includes ones that could be null because they're live
    virtual size_t my_size() { return sizeof(*this); }
    virtual InstancePtrBase* index_into_instance_vars(int num) { return nullptr; }
};