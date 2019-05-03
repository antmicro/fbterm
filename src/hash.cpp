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

#include <stdlib.h>
#include <unistd.h>
#include "hash.h"

template<class T> HashTable<T>::HashTable(u32 seed, bool autodel)
{
	mDestructing = false;
	mAutoDelete = autodel;
	mSize = 0;
	mSeed = seed;
	mTable = new Node*[seed];
	for (u32 i = 0; i < seed; i++) {
		mTable[i] = 0;
	}
}

template<class T> HashTable<T>::~HashTable()
{
	mDestructing = true;

	for (u32 i = 0; i < mSeed; i++) {
		Node *next, *cur = mTable[i];

		while (cur) {
			next = cur->next;
			if (mAutoDelete) delete cur->val;
			delete cur;
			cur = next;
		}
	}

	delete[] mTable;
}

template<class T> void HashTable<T>::add(u32 key, T &val)
{
	if (mDestructing) return;

	u32 index = key % mSeed;

	Node *node = new Node(key, val);
	node->next = mTable[index];

	mTable[index] = node;
	mSize++;
}

template<class T> void HashTable<T>::remove(u32 key)
{
	if (mDestructing) return;

	u32 index = key % mSeed;
	Node *prev, *cur;
	for (prev = 0, cur = mTable[index]; cur; prev = cur, cur = cur->next) {
		if (cur->key == key) {
			if (prev) prev->next = cur->next;
			else mTable[index] = cur->next;

			delete cur;
			mSize--;

			break;
		}
	}
}

template<class T> bool HashTable<T>::find(u32 key, T *pval)
{
	bool ret = false;
	if (pval) *pval = 0;

	for (Node *node = mTable[key % mSeed]; node; node = node->next) {
		if (node->key == key) {
			ret = true;
			if (pval) *pval = node->val;
			break;
		}
	}

	return ret;
}

template<class T> void HashTable<T>::values(T *pval, u32 &len)
{
	if (!pval || len == 0) return;

	u32 index = 0;
	for (u32 i = 0; i < mSeed; i++) {
		for (Node *cur = mTable[i]; cur; cur = cur->next) {
			pval[index++] = cur->val;
			if (index == len) return;
		}
	}

	len = mSize;
}

#include "io.h"
#include "font.h"
template class HashTable<IoPipe*>;
template class HashTable<Font::Glyph*>;
