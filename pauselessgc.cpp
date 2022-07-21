// pauselessgc.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <sstream>
#include <random>

#include "CollectableHash.h"

static int64_t identity_counter = 0;

class RandomCounted : public Collectable
{
public:
    int points_at_me;
    int64_t identity;
    InstancePtr<RandomCounted> first;
    InstancePtr<RandomCounted> second;

    void set_first(RootPtr<RandomCounted> o2, RootPtr<RandomCounted> o) {
        //memtest();
        assert(o2.var->owned);
        assert(o.get() == o2.get());
        if (nullptr != o.get()) ++o->points_at_me;
        if (nullptr != first.get())--(first->points_at_me);
        first = o;
    }
    void set_second(RootPtr<RandomCounted> o2, RootPtr<RandomCounted> o) {
        //memtest();
        assert(o2.var->owned);
        assert(o.get() == o2.get());
        if (nullptr != o.get()) ++o->points_at_me;
        if (nullptr != second.get())--second->points_at_me;
        second = o;
    }
    RandomCounted(int i) :points_at_me(0),identity(i) {}
    ~RandomCounted()
    {
//        if (0 == points_at_me) std::cout << "Correct delete\n";
//        else std::cout << "*** incorrect or cycle delete. Holds "<<points_at_me<<"\n";
    }
    int total_instance_vars() const {
        //memtest();
        return 2; }
    InstancePtrBase* index_into_instance_vars(int num) {
        //memtest();
        switch (num) {
        case 0: return &first;
        case 1: return &second;
        }
    }
    size_t my_size() const {
        //memtest();
        return sizeof(*this); }
};

const int Testlen = 100000;

RootPtr<CollectableString> int_to_string(int a)
{
    std::stringstream ss;
    ss << a;
    return cnew(CollectableString(ss.str().c_str()));
}

void mutator_thread()
{
    if (!GC::CombinedThread)GC::init_thread();
    
    thread_local RootPtr<RandomCounted>* bunch = new RootPtr<RandomCounted>[Testlen];
    thread_local  std::default_random_engine generator;
    thread_local std::uniform_int_distribution<int> distribution(0, Testlen - 1);

    RootPtr<CollectableHashTable<CollectableString,RandomCounted> > hash = cnew2template(CollectableHashTable<CollectableString, RandomCounted>());

    RootPtr<CollectableVector<RandomCounted> > vec= cnew (CollectableVector<RandomCounted>());
    for (int k = 1; k <= 5; ++k) {
        vec->clear();
        for (int i = 0; i < Testlen; ++i)
        {
            GC::safe_point();
            RootPtr<CollectableString> index = int_to_string(i);

            hash->insert_or_assign(index, cnew(RandomCounted(i)));
            int b = vec->size();
            vec->push_front(hash[index]);
            //assert(t);
            //int v = vec->size();
            //assert(v == i + 1);
            //assert(vec[0].get() == bunch[i].get());
        }
        //distribution(generator);  
        for (int j = 0; j < 2; ++j) {
            //GC::safe_point();
            for (int i = 0; i < Testlen; ++i)
            {
                RootPtr<CollectableString> ind = int_to_string(i);
                GC::safe_point();
                {
                    int j = distribution(generator);
                    RootPtr<CollectableString> jind = int_to_string(j);
                    //assert(j >= 0);
                    //assert(j < Testlen);
                    //assert(!bunch[i]->deleted);
                    //assert(!bunch[j]->deleted);
                    RootPtr<RandomCounted> jrc = hash[jind];
                    hash[ind]->set_first(jrc, jrc);
                    //bunch[i]->set_first(bunch[j], bunch[j]);
                    //assert(bunch[i].var->owned);
                    //assert(bunch[j].var->owned);
                }
                {
                    int j = distribution(generator);
                    RootPtr<CollectableString> jind = int_to_string(j);
                    RootPtr<RandomCounted> jrc = hash[jind];
                    //assert(j >= 0);
                    //assert(j < Testlen);
                    //assert(!bunch[i]->deleted);
                    //assert(!bunch[j]->deleted);
                    //bunch[i]->set_second(bunch[j], bunch[j]);
                    hash[ind]->set_second(jrc, jrc);
                    //assert(bunch[i].var->owned);
                    //assert(bunch[j].var->owned);
                }
            }

            for (int i = 0; i < Testlen >> 1; ++i)
            {
                GC::safe_point();
                RootPtr<CollectableString> index = int_to_string(Testlen - i - 1);

                hash->insert_or_assign(index, cnew(RandomCounted(Testlen - i - 1)));
                //bunch[i] = cnew(RandomCounted(Testlen-i-1));
                //assert(bunch[i].var->owned);
                //assert(!bunch[i]->deleted);
            }
        }
        for (int i = 0; i < Testlen; ++i) {
            int s = vec->size();
            assert(s == Testlen-i);
            assert(vec->back()->identity == i);
            RootPtr< RandomCounted> p;
            bool t = vec->pop_back(p);
            assert(t);
            int s2 = vec->size();
            int h = p->identity;
            assert(h == i);

        }
        int s = vec->size();
        assert(s == 0);
    }
}

int main()
{
    std::cout << "Hello World!\n";
    
    GC::init();

   //auto m2 = std::thread(mutator_thread);
    mutator_thread();
    GC::exit_collect_thread();
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
