#pragma once
#include "Collectable.h"
#include "spooky.h"


/* A simple hash table with the following assumptions:
1) they keys are strings up to length MAX_HASH_STRING_SIZE
a) the keys handed to the routines are constants and it's not up to the routines to own or clean them up.
b) strings will be copied into keys, and the Hash functions will manage that memory.
2) The table length has to be a power of 2 and will grow as necessary to mostly avoid collisions
3) the code relies on spooky hash

The hash table uses external lists for collisions but the table stays at least as big as twice the number of elements so there shouldn't be too many collisions.
The elements of the hash table are not pointers they're embedded Entry structs, so not very much external calls to new and delete will be necessary.
If you really want to avoid new and delete, then you can change the test at the end of HashInsert to expand earlier.

The original C version was probably a bit faster on expand and delete because it assumed that memory can be zeroed and copied.
This version uses constructors and destructors and std::move properly instead.
By the way, you should define a move constructor for your type that releases ownership of memory, if it's complicated enough to hold some.
It should also have an assignment operator.
 */

 /* for safety string operations should have a maximum size, they truncate after that. */
#define MAX_HASH_STRING_SIZE 1051

#define INITIAL_HASH_SIZE 1024

extern CollectableSentinal hash_marker;

template<typename K, typename V>
struct CollectableHashEntry 
{
	bool skip;
	InstancePtr<K> key;
	InstancePtr<V> value;
	CollectableHashEntry() :skip(false), key(nullptr), value(nullptr) {}
	int total_instance_vars() const { return 2; }
	InstancePtrBase* index_into_instance_vars(int num) { if (num == 0) return &key; return &value; }
};

template<typename K, typename V>
struct CollectableHashTable :public Collectable
{
	int HASH_SIZE;
	int used;
	mutable int wasted;
	InstancePtr<CollectableInlineVector<CollectableHashEntry<K,V>>> data;


	CollectableHashTable(int s= INITIAL_HASH_SIZE) :HASH_SIZE(s), used(0), wasted(0), data(cnew2template(CollectableInlineVector<CollectableHashEntry<K,V>>(s))) {}

	void inc_used()
	{
		++used;
		if (((used+wasted) << 2) > HASH_SIZE)
		{
			int OLD_HASH_SIZE = HASH_SIZE;
			HASH_SIZE <<= 1;
			RootPtr<CollectableInlineVector<CollectableHashEntry<K,V> > > t = data;
			data = cnew2template(CollectableInlineVector<CollectableHashEntry<K,V> >(HASH_SIZE));
			used = 0;
			wasted = 0;
			for (int i = 0; i < OLD_HASH_SIZE; ++i) {
				GC::safe_point();
				if (t[i]->key.get() != nullptr && !t[i]->skip) insert_or_assign(t[i]->key, t[i]->value);
			}
		}
	}
	bool findu(CollectableHashEntry<K, V>*&pair ,const RootPtr<K> &key, bool for_insert) const
	{
		uint64_t h = key->hash();
		int start = h & (HASH_SIZE - 1);
		int i = start;
		CollectableHashEntry<K, V>* recover=nullptr;
		GC::safe_point();
		do {
			CollectableHashEntry<K, V>* e = data[i];
			if (e->key.get() == nullptr) {
				if (recover != nullptr) {
					pair = recover;
					recover->skip = false;
					--wasted;
				}
				else pair = e;
				return false;
			}
			bool skip = e->skip;
			if (for_insert && skip)
			{
				recover = e;
				for_insert = false;
			}
			if (!skip && h == e->key->hash() && e->key->equal(key.get())) {
				pair = e;
				return true;
			}

			i = (i + 1) & (HASH_SIZE - 1);
		} while (i != start);
		return false;
	}

	bool contains(const RootPtr<K> &key) const {
		CollectableHashEntry<K, V>* pair = nullptr;
		return findu(pair, key, false);
	}
	RootPtr<V> operator[](const RootPtr<K>& key)
	{
		CollectableHashEntry<K, V>* pair = nullptr;
		if (findu(pair, key, false)) {

			return pair->value;
		}
		return (V *)nullptr;
	}
	bool insert(const RootPtr<K>& key, const RootPtr<V>& value)
	{
		CollectableHashEntry<K, V>* pair = nullptr;
		if (!findu(pair, key, true)) {
			pair->key = key;
			pair->value = value;
			inc_used();
			return true;
		}
		return false;
	}
	void insert_or_assign(const RootPtr<K>& key, const RootPtr<V>& value)
	{
		CollectableHashEntry<K, V>* pair = nullptr;
		findu(pair, key, true);
		bool replacing = pair->key.get() != nullptr;
		pair->key = key;
		pair->value = value;
		if (!replacing) inc_used();
	}
	bool erase(const RootPtr<K>& key)
	{
		CollectableHashEntry<K, V>* pair = nullptr;
		if (findu(pair, key, false)) {
			pair->skip = true;
			pair->key = nullptr;
			pair->value = nullptr;
			used = used - 1;
			++wasted;
			return true;
		}
		return false;
	}
	int size() const { return used; }

	virtual int total_instance_vars() const {
		return 1;
	}
	virtual size_t my_size() const { return sizeof(*this);  }
	virtual InstancePtrBase* index_into_instance_vars(int num) { return &data; }
};
