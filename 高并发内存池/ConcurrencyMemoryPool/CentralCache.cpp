#define  _CRT_SECURE_NO_WARNINGS
#pragma once

#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;  // 静态成员变量的定义


// 获取一个非空的span
// 当Central没有时，就需要向PageCache申请
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// 先检查当前Central的是否有非空的span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr) return it;
		it = it->_next;
	}
	
	// 先把central cache的桶锁解掉，这样如果其他线程释放内存对象回来，不会阻塞
	list._mtx.unlock();

	// 走到这里说明当前的Central没有非空的span
	// 需要向PageCache上申请

	PageCache::GetInstance()->_pageMtx.lock();
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true;
	span->_objSize = size;
	PageCache::GetInstance()->_pageMtx.unlock();

	// 获取到了
	// 对获取span进行切分，不需要加锁，因为这会其他线程访问不到这个span
	char* start = (char*)(span->_pageId << PAGE_SHIFT);
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;

	// 把大块内存切成自由链表链接起来
	// 1、先切一块下来去做头，方便尾插
	span->_freeList = start;
	start += size;
	void* tail = span->_freeList;
	while (start < end)
	{
		NextObj(tail) = start;
		tail = NextObj(tail);
		start += size;
	}

	NextObj(tail) = nullptr;

	//// 条件断点
	//int j = 0;
	//void* cur = span->_freeList;
	//while (cur)
	//{
	//	cur = NextObj(cur);
	//	++j;
	//}

	//if (j != (bytes / size))
	//{
	//	int x = 0;
	//}


	// 切好span以后，需要把span挂到桶里面去的时候，再加锁
	list._mtx.lock();
	list.PushFront(span);

	return span;
}

// 从中心缓存获取一定数量的对象给thread cache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock();

	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);

	// 从span中获取batchNum个对象
	// 如果不够batchNum个，有多少拿多少
	start = span->_freeList;
	end = start;
	size_t i = 0;
	size_t actualNum = 1;
	while (i < batchNum - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		++i;
		++actualNum;
	}
	// 实际只可以申请到 actualNum 个内存块

	// 申请完后，要修改原本CentralCache的 span
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;

	span->_useCount += actualNum;


	//// 条件断点 --> span切的有问题
	//int j = 0;
	//void* cur = start;
	//while (cur)
	//{
	//	cur = NextObj(cur);
	//	++j;
	//}

	//if (j != actualNum)
	//{
	//	int x = 0;
	//}

	_spanLists[index]._mtx.unlock();
	
	return actualNum;
}


// 将一定数量的对象释放到span跨度
void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock(); // 加锁

	while (start)
	{
		void* next = NextObj(start);
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		NextObj(start) = span->_freeList;
		span->_freeList = start;

		span->_useCount--; //更新被分配给thread cache的计数

		if (span->_useCount == 0) // 说明这个span分配出去的对象全部都回来了
		{
			// 此时这个span就可以再回收给page cache，page cache可以再尝试去做前后页的合并
			
			// 先处理span，做准备工作
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			// 释放span给page cache时，使用page cache的锁就可以了
			// 这时把桶锁解掉
			_spanLists[index]._mtx.unlock();

			PageCache::GetInstance()->_pageMtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			span->_isUse = true;
			PageCache::GetInstance()->_pageMtx.unlock();

			_spanLists[index]._mtx.lock();
		}

		start = next;
	}

	_spanLists[index]._mtx.unlock(); // 加锁
}