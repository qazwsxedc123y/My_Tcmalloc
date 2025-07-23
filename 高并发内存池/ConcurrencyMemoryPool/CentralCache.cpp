#define  _CRT_SECURE_NO_WARNINGS
#pragma once

#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;  // ��̬��Ա�����Ķ���


// ��ȡһ���ǿյ�span
// ��Centralû��ʱ������Ҫ��PageCache����
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// �ȼ�鵱ǰCentral���Ƿ��зǿյ�span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr) return it;
		it = it->_next;
	}
	
	// �Ȱ�central cache��Ͱ�������������������߳��ͷ��ڴ�����������������
	list._mtx.unlock();

	// �ߵ�����˵����ǰ��Centralû�зǿյ�span
	// ��Ҫ��PageCache������

	PageCache::GetInstance()->_pageMtx.lock();
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true;
	span->_objSize = size;
	PageCache::GetInstance()->_pageMtx.unlock();

	// ��ȡ����
	// �Ի�ȡspan�����з֣�����Ҫ��������Ϊ��������̷߳��ʲ������span
	char* start = (char*)(span->_pageId << PAGE_SHIFT);
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;

	// �Ѵ���ڴ��г�����������������
	// 1������һ������ȥ��ͷ������β��
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

	//// �����ϵ�
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


	// �к�span�Ժ���Ҫ��span�ҵ�Ͱ����ȥ��ʱ���ټ���
	list._mtx.lock();
	list.PushFront(span);

	return span;
}

// �����Ļ����ȡһ�������Ķ����thread cache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock();

	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);

	// ��span�л�ȡbatchNum������
	// �������batchNum�����ж����ö���
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
	// ʵ��ֻ�������뵽 actualNum ���ڴ��

	// �������Ҫ�޸�ԭ��CentralCache�� span
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;

	span->_useCount += actualNum;


	//// �����ϵ� --> span�е�������
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


// ��һ�������Ķ����ͷŵ�span���
void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock(); // ����

	while (start)
	{
		void* next = NextObj(start);
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		NextObj(start) = span->_freeList;
		span->_freeList = start;

		span->_useCount--; //���±������thread cache�ļ���

		if (span->_useCount == 0) // ˵�����span�����ȥ�Ķ���ȫ����������
		{
			// ��ʱ���span�Ϳ����ٻ��ո�page cache��page cache�����ٳ���ȥ��ǰ��ҳ�ĺϲ�
			
			// �ȴ���span����׼������
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			// �ͷ�span��page cacheʱ��ʹ��page cache�����Ϳ�����
			// ��ʱ��Ͱ�����
			_spanLists[index]._mtx.unlock();

			PageCache::GetInstance()->_pageMtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			span->_isUse = true;
			PageCache::GetInstance()->_pageMtx.unlock();

			_spanLists[index]._mtx.lock();
		}

		start = next;
	}

	_spanLists[index]._mtx.unlock(); // ����
}