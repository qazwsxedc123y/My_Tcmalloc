#pragma once


#include "common.h"

// 单例模式
// 一个类只可以实例化一个
class CentralCache
{
public:
	// 这个函数是单例模式的实现，获取CentralCache类的唯一实例
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}
	// 获取一个非空的span
	Span* GetOneSpan(SpanList& list, size_t size);

	// 从中心缓存获取一定数量的对象给thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);

	// 将一定数量的对象释放到span跨度
	void ReleaseListToSpans(void* start, size_t size);
private:
	// 其为一个CentralCache的所有自由链表的数组，所有线程共享CentralCache
	// 其结构还是一个哈希桶的结构
	SpanList _spanLists[MAX_FREE_LIST];

private:
	CentralCache()
	{
	}

	CentralCache(const CentralCache&) = delete;

	static CentralCache _sInst;
};