#pragma once
#include "Collectable.h"
#include "spooky.h"


/* A simple hash table with the following assumptions:
* 
1) the keys are any collectable type.  I added a hash function to collectables that defaults to being based on the address.  But I also added a string type.  All hashing is based on spookyhash.
2) the value type should be a collectable or at least something that has the total_instance_vars() and index_into_instance_vars(int) methods.  They will all be stored in a single block and the whole thing collected at once.
3) The table length has to be a power of 2 and will grow as necessary to mostly avoid collisions

The hash table uses linear probing and stays at least 4 times as big as the number of elements.  You can delete from this hash table, it handles that by marking elements "deleted" and not moving anything.

 */

#define INITIAL_HASH_SIZE 1024

extern CollectableSentinel CollectableNull;


template<typename K, typename V>
struct CollectableKeyHashEntry
{
	bool skip;
	bool empty;
	InstancePtr<K> key;
	V value;
	CollectableKeyHashEntry() :skip(false), empty(true), key(nullptr) {}
	int total_instance_vars() const { return 1; }
	InstancePtrBase* index_into_instance_vars(int num) { return &key;  }
};

template<typename K, typename V>
struct CollectableKeyHashTable :public Collectable
{
	int HASH_SIZE;
	int used;
	mutable int wasted;
	InstancePtr<CollectableInlineVector<struct CollectableKeyHashTable<K, V>>> data;


	CollectableKeyHashTable(int s = INITIAL_HASH_SIZE) :HASH_SIZE(s), used(0), wasted(0), data(cnew2template(CollectableInlineVector<CollectableKeyHashTable<K, V>>(s))) {}

	void inc_used()
	{
		++used;
		if (((used + wasted) << 2) > HASH_SIZE)
		{
			int OLD_HASH_SIZE = HASH_SIZE;
			HASH_SIZE <<= 1;
			RootPtr<CollectableInlineVector<CollectableKeyHashTable<K, V> > > t = data;
			data = cnew2template(CollectableInlineVector<CollectableKeyHashTable<K, V> >(HASH_SIZE));
			used = 0;
			wasted = 0;
			for (int i = 0; i < OLD_HASH_SIZE; ++i) {
				GC::safe_point();
				if (!t[i]->key.empty && !t[i]->skip) insert_or_assign(t[i]->key, t[i]->value);
			}
		}
	}
	bool findu(CollectableKeyHashEntry<K, V>*& pair, const RootPtr<K>& key, bool for_insert) const
	{
		uint64_t h = key->hash();
		int start = h & (HASH_SIZE - 1);
		int i = start;
		CollectableKeyHashEntry<K, V>* recover = nullptr;
		GC::safe_point();
		do {
			CollectableKeyHashEntry<K, V>* e = data[i];
			if (e->empty) {
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

	bool contains(const RootPtr<K>& key) const {
		CollectableKeyHashEntry<K, V>* pair = nullptr;
		return findu(pair, key, false);
	}
	V operator[](const RootPtr<K>& key)
	{
		CollectableKeyHashEntry<K, V>* pair = nullptr;
		if (findu(pair, key, false)) {

			return pair->value;
		}
		return V();
	}
	bool insert(const RootPtr<K>& key, const V& value)
	{
		CollectableKeyHashEntry<K, V>* pair = nullptr;
		if (!findu(pair, key, true)) {
			pair->key = key;
			pair->value = value;
			pair->empty = false;
			inc_used();
			return true;
		}
		return false;
	}
	void insert_or_assign(const RootPtr<K>& key, const V& value)
	{
		CollectableKeyHashEntry<K, V>* pair = nullptr;
		findu(pair, key, true);
		bool replacing = !pair->empty;
		pair->key = key;
		pair->value = value;
		pair->empty = false;
		if (!replacing) inc_used();
	}
	bool erase(const RootPtr<K>& key)
	{
		CollectableKeyHashEntry<K, V>* pair = nullptr;
		if (findu(pair, key, false)) {
			pair->skip = true;
			pair->key = nullptr;
			pair->empty = true;
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
	virtual size_t my_size() const { return sizeof(*this); }
	virtual InstancePtrBase* index_into_instance_vars(int num) { return &data; }
};

template<typename K, typename V>
struct CollectableValueHashEntry
{
	bool skip;
	bool empty;
	K key;
	InstancePtr<V> value;
	CollectableValueHashEntry() :skip(false),empty(true), value(nullptr) {}
	int total_instance_vars() const { return 1; }
	InstancePtrBase* index_into_instance_vars(int num) { return &value; }
};

template<typename K, typename V>
struct CollectableValueHashTable :public Collectable
{
	int HASH_SIZE;
	int used;
	mutable int wasted;
	InstancePtr<CollectableInlineVector<CollectableValueHashEntry<K, V>>> data;


	CollectableValueHashTable(int s = INITIAL_HASH_SIZE) :HASH_SIZE(s), used(0), wasted(0), data(cnew2template(CollectableInlineVector<CollectableValueHashEntry<K, V>>(s))) {}

	void inc_used()
	{
		++used;
		if (((used + wasted) << 2) > HASH_SIZE)
		{
			int OLD_HASH_SIZE = HASH_SIZE;
			HASH_SIZE <<= 1;
			RootPtr<CollectableInlineVector<CollectableValueHashEntry<K, V> > > t = data;
			data = cnew2template(CollectableInlineVector<CollectableValueHashEntry<K, V> >(HASH_SIZE));
			used = 0;
			wasted = 0;
			for (int i = 0; i < OLD_HASH_SIZE; ++i) {
				GC::safe_point();
				if (!t[i]->empty && !t[i]->skip) insert_or_assign(t[i]->key, t[i]->value);
			}
		}
	}
	bool findu(CollectableValueHashEntry<K, V>*& pair, const K& key, bool for_insert) const
	{
		uint64_t h = std::hash<K>(key);
		int start = h & (HASH_SIZE - 1);
		int i = start;
		CollectableValueHashEntry<K, V>* recover = nullptr;
		GC::safe_point();
		do {
			CollectableValueHashEntry<K, V>* e = data[i];
			if (e->empty) {
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
			if (!skip && h == std::hash<K>(e->key) && e->key == key) {
				pair = e;
				return true;
			}

			i = (i + 1) & (HASH_SIZE - 1);
		} while (i != start);
		return false;
	}

	bool contains(const K& key) const {
		CollectableValueHashEntry<K, V>* pair = nullptr;
		return findu(pair, key, false);
	}
	RootPtr<V> operator[](const K& key)
	{
		CollectableValueHashEntry<K, V>* pair = nullptr;
		if (findu(pair, key, false)) {

			return pair->value;
		}
		return (V*)nullptr;
	}
	bool insert(const K& key, const RootPtr<V>& value)
	{
		CollectableValueHashEntry<K, V>* pair = nullptr;
		if (!findu(pair, key, true)) {
			pair->key = key;
			pair->value = value;
			pair->empty = false;
			inc_used();
			return true;
		}
		return false;
	}
	void insert_or_assign(const K& key, const RootPtr<V>& value)
	{
		CollectableValueHashEntry<K, V>* pair = nullptr;
		findu(pair, key, true);
		bool replacing = !pair->empty;
		pair->key = key;
		pair->value = value;
		pair->empty = false;
		if (!replacing) inc_used();
	}
	bool erase(const K& key)
	{
		CollectableValueHashEntry<K, V>* pair = nullptr;
		if (findu(pair, key, false)) {
			pair->skip = true;
			pair->value = nullptr;
			pair->empty = true;
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
	virtual size_t my_size() const { return sizeof(*this); }
	virtual InstancePtrBase* index_into_instance_vars(int num) { return &data; }
};



template<typename K, typename V>
struct CollectableHashEntry 
{
	bool skip;
	bool empty;
	InstancePtr<K> key;
	InstancePtr<V> value;
	CollectableHashEntry() :skip(false), empty(true) {}
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
			RootPtr<CollectableInlineVector<CollectableHashEntry<K,V> > > t ( data);
			data = cnew2template(CollectableInlineVector<CollectableHashEntry<K,V> >(HASH_SIZE));
			used = 0;
			wasted = 0;
			for (int i = 0; i < OLD_HASH_SIZE; ++i) {
				GC::safe_point();
				if (!t[i]->empty && !t[i]->skip) insert_or_assign(t[i]->key, t[i]->value);
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
			if (e->empty) {
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
			pair->empty = false;
			inc_used();
			return true;
		}
		return false;
	}
	void insert_or_assign(const RootPtr<K>& key, const RootPtr<V>& value)
	{
		CollectableHashEntry<K, V>* pair = nullptr;
		findu(pair, key, true);
		bool replacing = !pair->empty;
		pair->key = key;
		pair->value = value;
		pair->empty = false;

		if (!replacing) inc_used();
	}
	bool erase(const RootPtr<K>& key)
	{
		CollectableHashEntry<K, V>* pair = nullptr;
		if (findu(pair, key, false)) {
			pair->skip = true;
			pair->key = nullptr;
			pair->value = nullptr;
		pair->empty = true;
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


template<typename K, typename V>
struct HashEntry
{
	bool skip;
	bool empty;
	K key;
	V value;
	HashEntry() :skip(false),empty(true), key(nullptr), value(nullptr) {}
};

template<typename K, typename V>
struct HashTable :public Collectable
{
	int HASH_SIZE;
	int used;
	mutable int wasted;
	std::vector<HashEntry<K, V>> data;


	HashTable(int s = INITIAL_HASH_SIZE) :HASH_SIZE(s), used(0), wasted(0), data( s) {}

	void inc_used()
	{
		++used;
		if (((used + wasted) << 2) > HASH_SIZE)
		{
			int OLD_HASH_SIZE = HASH_SIZE;
			HASH_SIZE <<= 1;
			std::vector<HashEntry<K, V>> t = data;
			data.clear();
			data.resize(HASH_SIZE);
			used = 0;
			wasted = 0;
			for (int i = 0; i < OLD_HASH_SIZE; ++i) {
				GC::safe_point();
				if (!t[i]-empty && !t[i]->skip) insert_or_assign(t[i]->key, t[i]->value);
			}
		}
	}
	bool findu(HashEntry<K, V>*& pair, const K& key, bool for_insert) const
	{
		uint64_t h = std::hash<K>(key);
		int start = h & (HASH_SIZE - 1);
		int i = start;
		HashEntry<K, V>* recover = nullptr;
		GC::safe_point();
		do {
			HashEntry<K, V>* e = data[i];
			if (e->empty) {
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
			if (!skip && h == std::hash<K>(e->key) && e->key == key) {
				pair = e;
				return true;
			}

			i = (i + 1) & (HASH_SIZE - 1);
		} while (i != start);
		return false;
	}

	bool contains(const K& key) const {
		HashEntry<K, V>* pair = nullptr;
		return findu(pair, key, false);
	}
	V operator[](const K& key)
	{
		HashEntry<K, V>* pair = nullptr;
		if (findu(pair, key, false)) {

			return pair->value;
		}
		return V();
	}
	bool insert(const K& key, const V& value)
	{
		HashEntry<K, V>* pair = nullptr;
		if (!findu(pair, key, true)) {
			pair->key = key;
			pair->value = value;
			pair->empty = false;
			inc_used();
			return true;
		}
		return false;
	}
	void insert_or_assign(const K& key, const V& value)
	{
		HashEntry<K, V>* pair = nullptr;
		findu(pair, key, true);
		bool replacing = pair->key.get() != nullptr;
		pair->key = key;
		pair->value = value;
		pair->empty = false;
		if (!replacing) inc_used();
	}
	bool erase(const RootPtr<K>& key)
	{
		HashEntry<K, V>* pair = nullptr;
		if (findu(pair, key, false)) {
			pair->skip = true;
			pair->empty = true;
			used = used - 1;
			++wasted;
			return true;
		}
		return false;
	}
	int size() const { return used; }

	virtual int total_instance_vars() const {
		return 0;
	}
	virtual size_t my_size() const { return sizeof(*this); }
	virtual InstancePtrBase* index_into_instance_vars(int num) { return nullptr; }
};

