#pragma once
#include "GCState.h"
#include <iostream>

//#define ENSURE_THROW(cond, exception)	\
//	do { int __afx_condVal=!!(cond); assert(__afx_condVal); if (!(__afx_condVal)){exception;} } while (false)
//#define ENSURE(cond)		ENSURE_THROW(cond, throw std::runtime_error(#cond " failed") )
//#define ENSURE(cond)
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

        CircularDoubleList& operator*() { return *center; }
        bool operator ++() { center = next; prev = center->prev; next = center->next;  return !center->sentinel(); }

        bool operator --() { center = prev; prev = center->prev; next = center->next;  return !center->sentinel(); }

        operator bool() { return !center->sentinel(); }
        bool operator !() { return center->sentinel(); }
    };
    //virtual void fake_delete() = 0;
    iterator iterate() { return iterator(*this); }
    bool sentinel() const { return is_sentinel; }

    virtual ~CircularDoubleList() { next->prev = prev; prev->next = next;}
    void disconnect() { next->prev = prev; prev->next = next; }
    CircularDoubleList(_sentinel_) :prev(this), next(this), is_sentinel(true) {}
    CircularDoubleList(_before_, CircularDoubleList* e) :prev(e->prev), next(e), is_sentinel(false)
    {
        next->prev = prev->next = this;
    }
    CircularDoubleList(_after_, CircularDoubleList* e) :prev(e), next(e->next),  is_sentinel(false)
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
    //bool deleted;

    virtual GC::SnapPtr* double_ptr() { abort(); return nullptr; }
    //void fake_delete() { deleted = true; disconnect(); }
    //void memtest() { 
        //ENSURE(!deleted); 
    //    if (deleted) std::cout << '.';
    //}
    RootLetterBase(RootLetterBase&&) = delete;
    RootLetterBase(_sentinel_): CircularDoubleList(_SENTINEL_),owned(true)//,deleted(false)
    {  }
    RootLetterBase();
    virtual void mark() { abort(); }
    virtual ~RootLetterBase()
    {
    }
};

inline RootLetterBase::RootLetterBase():CircularDoubleList(_START_,GC::ScanListsByThread[GC::MyThreadNumber]->roots[GC::ActiveIndex]),owned(true)//,deleted(false)
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
//public:
//    bool deleted;
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
    Collectable(_sentinel_) : CircularDoubleList(_SENTINEL_), back_ptr(nullptr) , marked(true)//, deleted(false)
    {
        
    }

public:
    //void memtest() const { 
        //ENSURE(!deleted); 
    //    if (deleted) std::cout << ':';
    //}
    //void fake_delete() {
    //    if (deleted) {
    //        std::cout << '$';
    //        return;
     //   }
     //   deleted = true; disconnect();  }
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
                       // if (n->deleted) std::cout << '*';

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
    virtual void clean_after_collect() {}
    Collectable(Collectable&&) = delete;

    Collectable() :CircularDoubleList(_START_, GC::ScanListsByThread[GC::MyThreadNumber]->collectables[GC::ActiveIndex]), back_ptr(nullptr), marked(false)//,deleted(false) 
    {
        }

};

inline int saturate32(int i)
{
    if (i > 32) return 32;
    return i;
}
inline int saturate1024(int i)
{
    if (i > 1024) return 1024;
    return i;
}
inline int saturate32768(int i)
{
    if (i > 32768) return 32768;
    return i;
}
inline int saturate1m(int i)
{
    if (i > 1048576) return 1048576;
    return i;
}

template<typename T>
struct CollectableBlock : public Collectable
{
    InstancePtr<T> block[32];
    uint8_t size;
    uint8_t reserved;

    size_t my_size() { return sizeof(this); }
    int total_instance_vars() { return reserved; }


    InstancePtrBase* index_into_instance_vars(int num) { return &block[num]; }
    bool push_back(RootPtr<T>& o) {
        if (size == 32) return false;
        block[size++] = o;
        if (size >= reserved) reserved = size;
        return true;
    }
    bool pop_back(RootPtr<T>& o) {
        if (size == 0) return false;
        o = block[--size];
        block[size] = nullptr;
        return true;
    }
    bool pop_back(InstancePtr<T>& o) {
        if (size == 0) return false;
        o = block[--size];
        block[size] = nullptr;
        return true;
    }
    CollectableBlock():size(0), reserved(0){}
    InstancePtr<T>& operator [] (int i) {
        return block[i & 31];
    }
    InstancePtr<T>& insure (int i) {
        int s = i+1;
        if (s > size) size = s;
        if (size >= reserved) reserved = size;
        return block[i & 31];
    }
    void clear()
    {
        for (int i = 0; i < size; ++i) block[i] = nullptr;
        size = 0;
    }
    void resize(int s)
    {
        assert(s < 32);
        while (size > s)block[--size] = nullptr;
        size = s;
    }
};
template<typename T>
struct Collectable2Block : public Collectable
{
    InstancePtr< CollectableBlock<T> > block[32];
    //uint8_t b_size;
    uint8_t b_reserved;
    int size;

    int b_size(int i=0) { return ((size+i-1) >> 5)+1; }
   

    size_t my_size() { return sizeof(this); }
    int total_instance_vars() { return b_reserved; }
    InstancePtrBase* index_into_instance_vars(int num) { return &block[num]; }
    Collectable2Block() :size(0), b_reserved(0) {}
    bool push_back(RootPtr<T>& o) {
        if (size == 32*32) return false;
        ++size;
        if (b_size() > b_size(-1)) {
            if (block[b_size() - 1].get() == nullptr) block[b_size() - 1] = new CollectableBlock<T>;
            if (b_size() > b_reserved) b_reserved = b_size();
        }
        return block[b_size() - 1]->push_back(o);
    }

    bool pop_back(RootPtr<T>& o)
    {
        if (size == 0) return false;
        block[b_size() - 1]->pop_back(o);
        --size;
        //b_size = ((--size-1) >> 5)+1;
        return true;
    }

    bool pop_back(InstancePtr<T>& o)
    {
        if (size == 0) return false;
        block[b_size() - 1]->pop_back(o);
        --size;
        //b_size = ((--size-1) >> 5) + 1;
        return true;
    }

    InstancePtr<T>& operator [] (int i) {
        return block[31 & (i >> 5)]->operator[](i);
    }
    InstancePtr<T>& insure(int i) {
        int j = 31&(i >> 5);
        for (int k = b_size()-1; k <= j; ++k) {
            if (block[k].get() == nullptr) {
                block[k] = new CollectableBlock<T>;
                if (k<j) block[k]->insure((1<<6)-1);
            }
        }
        int s = i+1;
        if (s > size) size = s;
        if (b_reserved<b_size()) b_reserved = b_size();
        

        return block[j]->insure(i-(j<<5));
    }
    void clear()
    {
        GC::safe_point();
        for (int i = 0; i < b_size(); ++i) block[i]->clear();
        size = 0;
    }
    void resize(int s)
    {
        assert(s < 32*32);
        if (s == size) return;
        if (s > size) {
            insure(s - 1);
        }
        else {
            int j = (s - 1) >> 5;
            block[j]->resize(s - (j << 5) + 1);
            for (int k = j + 1; k < b_size(); ++k) block[k]->clear();
            size = s;
        }
    }
};

template<typename T>
struct Collectable3Block : public Collectable
{
    InstancePtr< Collectable2Block<T> > block[32];
    int b_size(int i = 0) { return ((size + i - 1) >> 10) + 1; }
    uint8_t b_reserved;
    int size;



    size_t my_size() { return sizeof(this); }
    int total_instance_vars() { return b_reserved; }
    Collectable3Block() :size(0), b_reserved(0) {}
    InstancePtrBase* index_into_instance_vars(int num) { return &block[num]; }
    bool push_back(RootPtr<T>& o) {
        if (size == 32 * 32 * 32) return false;
        ++size;
        if (b_size() > b_size(-1)) {
            if (block[b_size()-1].get() == nullptr) block[b_size()-1] = new Collectable2Block<T>;
            if (b_size() > b_reserved) b_reserved = b_size();
        }
        return block[b_size() - 1]->push_back(o);
    }
    bool pop_back(RootPtr<T>& o)
    {
        if (size == 0) return false;
        block[b_size() - 1]->pop_back(o);
        --size;
        //b_size = ((--size-1) >> 5)+1;
        return true;
    }
    bool pop_back(InstancePtr<T>& o)
    {
        if (size == 0) return false;
        block[b_size() - 1]->pop_back(o);
        --size;
        //b_size = ((--size-1) >> 5)+1;
        return true;
    }
    InstancePtr<T>& operator [] (int i) {
        return block[31&(i >> 10)]->operator [](i);
    }
    InstancePtr<T>& insure(int i) {
        int j = 31 & (i >> 10);
        for (int k = b_size() - 1; k <= j; ++k) {
            if (block[k].get() == nullptr) {
                block[k] = new Collectable2Block<T>;
                if (k < j) block[k]->insure((1 << 11) - 1);
            }
        }
        int s = i+1;
        if (s > size) size = s;        
        if (b_reserved < b_size()) b_reserved = b_size();


        return block[j]->insure(i-(j<<10));
    }
    void clear()
    {
        for (int i = 0; i < b_size(); ++i) {
            block[i]->clear();
            GC::safe_point();
        }
     
        size = 0;
    }
    void resize(int s)
    {
        assert(s < 32*32*32);
        if (s == size) return;
        if (s > size) {
            insure(s - 1);
        }
        else {
            int j = (s - 1) >> 10;
            block[j]->resize(s - (j << 10) + 1);
            for (int k = j + 1; k < b_size(); ++k) block[k]->clear();
            size = s;
        }
    }
};

template<typename T>
struct Collectable4Block : public Collectable
{
    InstancePtr< Collectable3Block<T> > block[32];

    uint8_t b_reserved;
    int size;
    int b_size(int i = 0) { return ((size + i - 1) >> 15)+1; }

    size_t my_size() { return sizeof(this); }
    int total_instance_vars() { return b_reserved; }
    Collectable4Block() :size(0), b_reserved(0) {}
    InstancePtrBase* index_into_instance_vars(int num) { return &block[num]; }
    bool push_back(RootPtr<T>& o) {
        if (size == 32 * 32 * 32) return false;
        ++size;
        if (b_size() > b_size(-1)) {
            if (block[b_size() - 1].get() == nullptr) block[b_size() - 1] = new Collectable3Block<T>;
            if (b_size() > b_reserved) b_reserved = b_size();
        }
        return block[b_size() - 1]->push_back(o);

    }
    bool pop_back(RootPtr<T>& o)
    {
        if (size == 0) return false;
        block[b_size() - 1]->pop_back(o);
        --size;
        //b_size = ((--size-1) >> 5)+1;
        return true;
    }
    bool pop_back(InstancePtr<T>& o)
    {
        if (size == 0) return false;
        block[b_size() - 1]->pop_back(o);
        --size;
        //b_size = ((--size-1) >> 5)+1;
        return true;
    }
    InstancePtr<T>& operator [] (int i) {
        return block[31 & (i >> 15)]->operator [](i);
    }

    InstancePtr<T>& insure(int i) {
        int j = 31 & (i >> 15);
        for (int k = b_size() - 1; k <= j; ++k) {
            if (block[k].get() == nullptr) {
                block[k] = new Collectable3Block<T>;
                if (k < j) block[k]->insure((1 << 16) - 1);
            }
        }
        int s = i+1;
        if (s > size) size = s;        
        if (b_reserved < b_size()) b_reserved = b_size();

        return block[j]->insure(i-(j<<15));
    }

    bool push_front(RootPtr<T>& o) {
        if (size == 32 * 32 * 32 * 32) return false;
        if (size > 0) {
            insure(size) = (*this)[size - 1];
            for (int i = size - 1; i > 0; --i) {
                if ((i&1023)==0) GC::safe_point();
                (*this)[i] = (*this)[i - 1];
            }
            (*this)[0] = o;
        }
        else return push_back(o);
        return true;
    }

    void clear()
    {
        for (int i = 0; i < b_size(); ++i) {
            GC::safe_point();
            block[i]->clear();
        }
        size = 0;
    }
    void resize(int s)
    {
        assert(s < 32*32*32*32);
        if (s == size) return;
        if (s > size) {
            insure(s - 1);
        }
        else {
            int j = (s-1) >> 15;
            block[j]->resize(s - (j << 15)+1);
            for (int k = j + 1; k < b_size(); ++k) block[k]->clear();
            size = s;
        }
    }
};

template<typename T>
class VectorOfCollectable;

template<typename T>
struct CollectableVectoreUse : public Collectable
{
    int size;
    int scan_size;
    int reserved;

    std::unique_ptr<InstancePtr<T> > data;

    CollectableVectoreUse(int s) :size(0), scan_size(0),reserved(s), data(new InstancePtr<T>[s]) {}
    int total_instance_vars() {
        return scan_size;
    }
    InstancePtrBase* index_into_instance_vars(int num) {
        return data.get() + num;
    }
    size_t my_size() { return sizeof(*this) + sizeof(InstancePtr<T>) * reserved; }


    bool push_back(RootPtr<T>& o) {
        if (size >= reserved) return false;
        (data.get())[size++] = o;
        if (size > scan_size) scan_size = size;
        return true;
    }
    bool pop_back(RootPtr<T>& o) {
        if (size == 0) return false;
        o = (data.get())[--size];
        (data.get())[size] = nullptr;
        return true;
    }
    bool pop_back(InstancePtr<T>& o) {
        if (size == 0) return false;
        o = (data.get())[--size];
        (data.get())[size] = nullptr;
        return true;
    }
    InstancePtr<T>& at (int i) {
        return (data.get())[i];
    }

    void clear()
    {
        for (int i = 0; i < size; ++i) (data.get())[i] = nullptr;
        size = 0;
    }
    bool resize(int s, RootPtr<T>& exemplar)
    {
        if (s > reserved) return false;
        if (s < reserved) while (size > s)(data.get())[--size] = nullptr;
        else while (size < s)(data.get())[size++] = exemplar;
        if (size > scan_size) scan_size = size;
        return true;
    }
    bool resize(int s, InstancePtr<T>& exemplar)
    {
        if (s > reserved) return false;
        if (s < reserved) while (size > s)(data.get())[--size] = nullptr;
        else while (size < s)(data.get())[size++] = exemplar;
        if (size > scan_size) scan_size = size;
        return true;
    }
    bool resize(int s)
    {
        if (s > reserved) return false;
        if (s < reserved) while (size > s)(data.get())[--size] = nullptr;
        size = s;
        return true;
    }
    bool push_front(RootPtr<T>& o)
    {
        if (size >= reserved) return false;
        if (size > 0) {
            (*this)[size] = (*this)[size - 1];
            for (int i = size - 1; i > 0; --i) {
                if ((i & 1023) == 0) GC::safe_point();
                (*this)[i] = (*this)[i - 1];
            }
            (*this)[0] = o;
            ++size;
        }
        else return push_back(o);
        if (size > scan_size) scan_size = size;
        return true;
    }
};

template<typename T>
class CollectableVector : public Collectable
{
    InstancePtr<CollectableVectoreUse<T> > data;
    public:
    int total_instance_vars() {
        return 1;
    }
    InstancePtrBase* index_into_instance_vars(int num) {
        return &data;
    }
    size_t my_size() { return sizeof(*this); }


    CollectableVector() : data(new CollectableVectoreUse<T>(8)){}
    CollectableVector(int s) : data(new CollectableVectoreUse<T>(s<<1)){}
    CollectableVector(int s, RootPtr<T>& exemplar) : data(new CollectableVectoreUse<T>(s << 1)){ resize(s, exemplar); }
    CollectableVector(int s, InstancePtr<T>& exemplar) : data(new CollectableVectoreUse<T>(s << 1)) { resize(s, exemplar); }
    void push_back(RootPtr<T>& o)
    {
        if (!data->push_back(o)) {
            resize(size() + 1);
            data->push_back(o);
        }
    }
    int size() { return data->size; }
    bool pop_back(RootPtr<T>& o) 
    {
        return data->pop_back(o);
    }
    bool pop_back(InstancePtr<T>& o)
    {
        return data->pop_back(o);
    }

    InstancePtr<T>& at (int i)
    {
        return data->data.get()[i];
    }
    void clear() { data->clear();  }

    void resize(int s, RootPtr<T>& exemplar)
    {
        if (!data->resize(s, exemplar)) {
            RootPtr<CollectableVectoreUse<T> > data_held_for_collect = data;
            data = new CollectableVectoreUse<T>(this, s << 1);
            InstancePtr<T>* source = data_held_for_collect.data.get();
            InstancePtr<T>* dest = data.data.get();

            int i;
            for (i = 0; i < data_held_for_collect->size; ++i) {
                if ((i & 1023) == 0) GC::safe_point();
                dest[i] = source[i];
            }
            for (; i < data->size; ++i) {
                if ((i & 1023) == 0) GC::safe_point();
                dest[i] = exemplar;
            }
            data->size = s;
        }
    }   void resize(int s, InstancePtr<T>& exemplar)
    {
        if (!data->resize(s,exemplar)) {
            RootPtr<CollectableVectoreUse<T> > data_held_for_collect = data;
            data = new CollectableVectoreUse<T>(this, s << 1);
            InstancePtr<T>* source = data_held_for_collect.data.get();
            InstancePtr<T>* dest = data.data.get();

            int i;
            for (i = 0; i < data_held_for_collect->size; ++i) { 
                if ((i & 1023) == 0) GC::safe_point();
                dest[i] = source[i];
            }
            for (; i < data->size; ++i) {
                if ((i & 1023) == 0) GC::safe_point();
                dest[i] = exemplar;
            }
            data->size = s;
        }
    }

    void resize(int s)
    {
        if (!data->resize(s)) {
            RootPtr<CollectableVectoreUse<T> > data_held_for_collect = data;
            data = new CollectableVectoreUse<T>(this, s << 1);
            InstancePtr<T> * source = data_held_for_collect.data.get();
            InstancePtr<T> * dest = data.data.get();

            for (int i = data_held_for_collect->size - 1; i >= 0; --i) { 
                if ((i & 1023) == 0) GC::safe_point();
                dest[i] = source[i];
            }
            data.size = data_held_for_collect->size;
        }
    }
    void push_front(RootPtr<T>& o) {
        int s = size()+1;
        if (!data->resize(s)) {
            RootPtr<CollectableVectoreUse<T> > data_held_for_collect = data;
            data = new CollectableVectoreUse<T>(s << 1);
            InstancePtr<T>* source = data_held_for_collect->data.get();
            InstancePtr<T>* dest = data->data.get();

            int i;
            for (i = s-2; i >= 0; --i) {
                if ((i & 1023) == 0) GC::safe_point();
                dest[i+1] = source[i];
            }
            dest[0] = o;
            data->size = s;
        }
        else {
            InstancePtr<T>* dest = data->data.get();
            int i;
            for (i = s - 2; i >= 0; --i) {
                if ((i & 1023) == 0) GC::safe_point();
                dest[i + 1] = dest[i];
            }
            dest[0] = o;
            data->size = s;
        }
    }
    InstancePtr<T>& front() {
        return at(0);
    }
    InstancePtr<T>& back() {
        return at(size() - 1);
    }

};

template<typename T>
class SharableVector : Collectable
{
    InstancePtr< Collectable4Block<T> > blocks;
public:
    int total_instance_vars() {
        return 1;
    }
    InstancePtrBase* index_into_instance_vars(int num) {
        return &blocks;
    }
    size_t my_size() { return sizeof(*this); }


    SharableVector() :blocks(new Collectable4Block<T>) {}
    bool push_back(RootPtr<T>& o) 
    {
        return blocks->push_back(o);
    }
    bool pop_back(RootPtr<T>& o)
    {
        return blocks->pop_back(o);
    }
    bool pop_back(InstancePtr<T>& o)
    {
        return blocks->pop_back(o);
    }
    bool push_front(RootPtr<T>& o)
    {
        return blocks->push_front(o);
    }
    void clear()
    {
        blocks->clear();
    }
    void resize(int s)
    {
        blocks->resize(s);
    }
    int size() { return blocks->size; }
    InstancePtr<T>& at (int i) {
        return (*blocks.get())[i];
    }
    InstancePtr<T>& insure (int i) {
        return blocks->insure(i);
    }
    InstancePtr<T>& front() {
        return at(0);
    }
    InstancePtr<T>& back() {
        return at(size()-1);
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