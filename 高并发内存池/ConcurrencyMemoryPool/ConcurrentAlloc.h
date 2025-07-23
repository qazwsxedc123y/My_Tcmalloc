#pragma once

#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"
#include "ObjectPool.h"


// 封装，当有线程/进程申请内存时
// 会向ThreaCache上获取内存块
// 其会调用Allocate
static void* ConcurrentAlloc(size_t size)
{
	// 通过TLS 每个线程无锁的获取自己的专属的ThreadCache对象
	// 如果是第一次，那么此时的pTLSThreadCache为空，创建一个
	// 后续就不需要创建了，因为已经存在了
	if (size > MAX_BYTES)
	{
		// 分两种情况
		// 1.可以由单个的128页提供
		// 2.只能单独向堆上申请
		size_t alignSize = SizeClass::RoundUp(size);
		size_t kpage = alignSize >> PAGE_SHIFT;

		// 向page cache单独索要

		PageCache::GetInstance()->_pageMtx.lock();
		Span* span = PageCache::GetInstance()->NewSpan(kpage);
		span->_objSize = size;
		PageCache::GetInstance()->_pageMtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else
	{
		if (pTLSThreadCache == nullptr)
		{
			// 通过TLS 每个线程无锁的获取自己的专属的ThreadCache对象
			static ObjectPool<ThreadCache> tcPool;
			// pTLSThreadCache = new ThreadCache;
			pTLSThreadCache = tcPool.New();
		}

		// cout << std::this_thread::get_id() << ":" << pTLSThreadCache << endl;
		return pTLSThreadCache->Allocate(size);
	}

}

// 其该函数也是封装
// 其会调用Deallocate，其作用是当有线程/进程释放用tcmalloc申请的内存时
// 会调用该函数，还给ThreadCache
// 传参传 size 是为了计算其桶的位置
static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t size = span->_objSize;

	if (size > MAX_BYTES)
	{
		PageCache::GetInstance()->_pageMtx.lock();
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_pageMtx.unlock();
	}
	else
	{
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, size);
	}
}