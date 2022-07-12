# pauselessgc

A precise garbage collector for C++ that supports multiple mutating threads.  Threads have to sync at GC phase changes but other than that, threads are never stopped. 

In order to use it, you have to create types that can tell the collector how many pointers they contain and supply them one by one to be traced.   There is no support for resurrecting any objects on finalization. 

There are two pointer types, one goes inside of collectable objects.  The other is for root variables and for keeping temporary values on the stack from being collected. 

There are no limitations on sharing live objects between threads.  Like Java or .net the collector has no problem with collecting objects that are being contended on from multiple threads. 

The basic design is that each pointer is actually a pair of pointers, and when collection is not going on, each store to a pointer is actually an atomic, but not expensive, store to two pointers.  When collection starts, the stores are only to one of the two pointers and the other pointer is considered part of a snapshot that the collector follows.  When collection is done, stores go back to being to both pointers, meanwhile the collection threads are restoring the snapshot for pointers that were mutated during the collection.  Doing this requires things that aren't portably available in C++ and which aren't even available on all processors.  A 128 bit atomic store and load (without a fence) and a 128 bit atomic compare exchange are needed. 

Everything happens in-place, no compaction ever takes place. 

Mutating threads have to periodically go through safe-points in order to flush caches, change the write barrier and a few other small tasks in sync. When a mutating thread makes a blocking call, it should opt out of mutating before the call and opt back in afterwards so it doesn't hold up the other threads or the garbage collector.  There are RAII objects to automate that.  Another result of this design is that may be a bad idea to have more threads active than you have hyperthreads available on the processor, otherwise syncing will be slower.  It does yield threads while waiting in order to speed it up in that . 


# I believe this is a novel garbage collector.

A) It's the only parallel garbage collector I know of that never needs to pause mutator threads to scan roots or the stack or to catch up on dirty pointers.  It should have the least delay of any gc if used properly and that makes it useful for real time applications.

B) Garbage collectors for C++ are rare.  I have also already started on a portable version.  It would be much more complex because it has to use 32 bit indexes in the place of pointers, and so all objects have to be allocated within blocks. A Rust version may be in the future as well.

C) The number of memory fenced interlocked instructions needed is very much smaller than reference counted pointers, so this should be faster.

D) Any number of mutator threads are allowed.  I may expand to to have multple collection threads as well. 

E)  It also allows the collector to live on the same thread as a mutator, so you can have single threaded programs or all threads working all of the time.

Joshua Scholar 2022
