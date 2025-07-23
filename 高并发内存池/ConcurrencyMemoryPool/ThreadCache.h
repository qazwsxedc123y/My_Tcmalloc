#pragma once

#include "Common.h"

class ThreadCache
{
public:
	// 申请和释放内存对象
	void* Allocate(size_t size);
	void Deallocate(void* ptr, size_t size);

	// 从中心缓存获取对象
	// 当某个自由链表为空时，无法满足申请的空间，就需要向CentralCache获取对象
	// 然后ThreadCache有了足够的内存块
	void* FetchFromCentralCache(size_t index, size_t size);

	// 释放对象导致链表过长，回收内存到中心缓存
	void ListTooLong(FreeList& list, size_t size);

private:
	// 其为一个ThreadCache的所有自由链表的数组，其为单个线程的所有ThreadCache
	// 其结构还是一个哈希桶的结构
	FreeList _freeLists[MAX_FREE_LIST];
};

// TLS thread local storage
// 其定义了一个 线程局部存储 (Thread-Local Storage, TLS) 的静态指针变量
// _declspec(thread)，Windows 平台特有的关键字
// 表示该变量是 线程局部存储（TLS），即每个线程都会拥有该变量的独立副本，线程间互不干扰。
// ThreadCache* pTLSThreadCache
// 定义一个指向 ThreadCache 类对象的指针变量，变量名为 pTLSThreadCache
// 初始化为nullptr
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;