#pragma once

#include "Common.h"
#include "ObjectPool.h"
#include "PageMap.h"

// 1.page cache是一个以页为单位的span自由链表
// 2.为了保证全局只有唯一的page cache，这个类被设计成了单例模式。
class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}

	// 获取一个K页的span
	Span* NewSpan(size_t k);

	//获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);

	// 释放空闲span回到Pagecache，并合并相邻的span
	void ReleaseSpanToPageCache(Span* span);

	std::mutex _pageMtx;
private:
	SpanList _spanLists[NPAGES];
	ObjectPool<Span> _spanPool;

	// 只保存 page cache分配span给central cache 的span
	//std::unordered_map<PAGE_ID, Span*> _idSpanMap; // 第一个存页号，第二个存span
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;

	PageCache()
	{ }
	PageCache(const PageCache&) = delete;

	static PageCache _sInst;
};