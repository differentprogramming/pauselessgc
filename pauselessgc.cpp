// pauselessgc.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <random>

#include "Collectable.h"

static int64_t identity_counter = 0;

class RandomCounted : public Collectable
{
public:
    int points_at_me;
    int64_t identity;
    InstancePtr<RandomCounted> first;
    InstancePtr<RandomCounted> second;

    void set_first(RootPtr<RandomCounted> o2, RootPtr<RandomCounted> &o) {
        //memtest();
        assert(o2.var->owned);
        assert(o.get() == o2.get());
        if (nullptr != o.get()) ++o->points_at_me;
        if (nullptr != first.get())--(first->points_at_me);
        first = o;
    }
    void set_second(RootPtr<RandomCounted> o2, RootPtr<RandomCounted>& o) {
        //memtest();
        assert(o2.var->owned);
        assert(o.get() == o2.get());
        if (nullptr != o.get()) ++o->points_at_me;
        if (nullptr != second.get())--second->points_at_me;
        second = o;
    }
    RandomCounted() :points_at_me(0),identity(++identity_counter) {}
    ~RandomCounted()
    {
//        if (0 == points_at_me) std::cout << "Correct delete\n";
//        else std::cout << "*** incorrect or cycle delete. Holds "<<points_at_me<<"\n";
    }
    int total_instance_vars() {
        //memtest();
        return 2; }
    InstancePtrBase* index_into_instance_vars(int num) {
        //memtest();
        switch (num) {
        case 0: return &first;
        case 1: return &second;
        }
    }
    size_t my_size() {
        //memtest();
        return sizeof(*this); }
};

const int Testlen = 1000000;

void mutator_thread()
{
    if (!GC::CombinedThread)GC::init_thread();
    
    thread_local RootPtr<RandomCounted>* bunch = new RootPtr<RandomCounted>[Testlen];
    thread_local  std::default_random_engine generator;
    thread_local std::uniform_int_distribution<int> distribution(0, Testlen - 1);


    for (int i = 0; i < Testlen; ++i)
    {
        GC::safe_point();
        bunch[i] = cnew(RandomCounted);
    }
    //distribution(generator);  
    for (int j=0;j<20;++j) {
        //GC::safe_point();
        for (int i = 0; i < Testlen; ++i)
        {
            GC::safe_point();
            {
                int j = distribution(generator);
                assert(j >= 0);
                assert(j < Testlen);
                //assert(!bunch[i]->deleted);
                //assert(!bunch[j]->deleted);
                bunch[i]->set_first(bunch[j], bunch[j]);
                assert(bunch[i].var->owned);
                assert(bunch[j].var->owned);
            }
            {
                int j = distribution(generator);
                assert(j >= 0);
                assert(j < Testlen);
                //assert(!bunch[i]->deleted);
                //assert(!bunch[j]->deleted);
                bunch[i]->set_second(bunch[j], bunch[j]);
                assert(bunch[i].var->owned);
                assert(bunch[j].var->owned);
            }
        }

        for (int i = 0; i < Testlen >> 1; ++i)
        {
            GC::safe_point();
            bunch[i] = cnew(RandomCounted);
            assert(bunch[i].var->owned);
            assert(!bunch[i]->deleted);
        }
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
