#pragma once

#include "ThreadCache.h"
#include "CentralCache.h"

// 从 CentralCache 申请内存块
// 当线程的本地缓存（ThreadCache）为空时，从中心缓存（CentralCache）批量获取内存对象。
// 根据请求的 size（对象大小）和 index（自由列表的桶下标），
// 获取一批连续的内存块，填充到 ThreadCache 的自由列表中。
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// 慢开始反馈调节算法
	// 1、最开始不会一次向central cache一次批量要太多，因为要太多了可能用不完
	// 2、如果你不要这个size大小内存需求，那么batchNum就会不断增长，直到上限
	// 3、size越大，一次向central cache要的batchNum就越小
	// 4、size越小，一次向central cache要的batchNum就越大
	size_t batchNum = min(SizeClass::NumMoveSize(size), _freeLists[index].MaxSize());
	// 向 CentralCache 要batchNum个内存块
	if (_freeLists[index].MaxSize() == batchNum)
	{
		_freeLists[index].MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr;

	// 考虑FetchRangeObj有问题
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	assert(actualNum > 0);

	if (actualNum == 1)
	{
		assert(start == end);
		return start;
	}
	else
	{
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
		return start;
	}
}

// 申请
// 向ThreadCache申请内存时，首先要知道其位于那个桶的自由链表
// 然后申请
// 如果该桶为空，就向Central获取
// 反之可以直接申请
void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);

	 size_t alignSize = SizeClass::RoundUp(size);
	 size_t index = SizeClass::Index(size);

	if (!_freeLists[index].Empty())
	{
		return _freeLists[index].Pop();
	}
	else
	{
		return FetchFromCentralCache(index, alignSize);
	}
}

// 释放对象导致链表过长，回收内存到中心缓存
void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;

	//从list中取出一次批量个数的对象
	list.PopRange(start, end, list.MaxSize());

	//将取出的对象还给central cache中对应的span
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}

// 释放
// 这里的参数 size 是内存块对象的大小，不是个数
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);

	// 找对映射的自由链表桶，对象插入进入
	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);
	

	// 当然，为了避免某个threadcahce占用太多内存对象
	// 导致别的threadcache饥饿的状态
	// 也要设置threadcache向centralcache归还

	// 当自由链表长度大于一次批量申请的对象个数时就开始还一段list给central cache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		ListTooLong(_freeLists[index], size);
	}
}
