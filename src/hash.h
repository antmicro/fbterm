/*
 *   Copyright © 2008 dragchan <zgchan317@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2
 *   of the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef HASH_H
#define HASH_H

#include "type.h"

template<class T>
class HashTable {
public:
	HashTable(u32 seed, bool autodel);
	~HashTable();

	void add(u32 key, T &val);
	void remove(u32 key);
	bool find(u32 key, T *pval);
	void values(T *pval, u32 &len);
	u32 size() {
		return mSize;
	}

private:
	template<class T1> struct HashNode {
		HashNode(u32 _key ,T1 &_val) : key(_key), val(_val), next(0) {
		}
	
		u32 key;
		T1 val;
		HashNode *next;
	};

	typedef HashNode<T> Node;

	Node **mTable;
	u32 mSeed, mSize;
	bool mAutoDelete, mDestructing;
};

#endif
