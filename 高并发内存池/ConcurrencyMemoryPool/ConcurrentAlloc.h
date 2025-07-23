#pragma once

#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"
#include "ObjectPool.h"


// ��װ�������߳�/���������ڴ�ʱ
// ����ThreaCache�ϻ�ȡ�ڴ��
// ������Allocate
static void* ConcurrentAlloc(size_t size)
{
	// ͨ��TLS ÿ���߳������Ļ�ȡ�Լ���ר����ThreadCache����
	// ����ǵ�һ�Σ���ô��ʱ��pTLSThreadCacheΪ�գ�����һ��
	// �����Ͳ���Ҫ�����ˣ���Ϊ�Ѿ�������
	if (size > MAX_BYTES)
	{
		// ���������
		// 1.�����ɵ�����128ҳ�ṩ
		// 2.ֻ�ܵ������������
		size_t alignSize = SizeClass::RoundUp(size);
		size_t kpage = alignSize >> PAGE_SHIFT;

		// ��page cache������Ҫ

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
			// ͨ��TLS ÿ���߳������Ļ�ȡ�Լ���ר����ThreadCache����
			static ObjectPool<ThreadCache> tcPool;
			// pTLSThreadCache = new ThreadCache;
			pTLSThreadCache = tcPool.New();
		}

		// cout << std::this_thread::get_id() << ":" << pTLSThreadCache << endl;
		return pTLSThreadCache->Allocate(size);
	}

}

// ��ú���Ҳ�Ƿ�װ
// ������Deallocate���������ǵ����߳�/�����ͷ���tcmalloc������ڴ�ʱ
// ����øú���������ThreadCache
// ���δ� size ��Ϊ�˼�����Ͱ��λ��
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