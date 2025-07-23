#pragma once

#include "ThreadCache.h"
#include "CentralCache.h"

// �� CentralCache �����ڴ��
// ���̵߳ı��ػ��棨ThreadCache��Ϊ��ʱ�������Ļ��棨CentralCache��������ȡ�ڴ����
// ��������� size�������С���� index�������б��Ͱ�±꣩��
// ��ȡһ���������ڴ�飬��䵽 ThreadCache �������б��С�
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// ����ʼ���������㷨
	// 1���ʼ����һ����central cacheһ������Ҫ̫�࣬��ΪҪ̫���˿����ò���
	// 2������㲻Ҫ���size��С�ڴ�������ôbatchNum�ͻ᲻��������ֱ������
	// 3��sizeԽ��һ����central cacheҪ��batchNum��ԽС
	// 4��sizeԽС��һ����central cacheҪ��batchNum��Խ��
	size_t batchNum = min(SizeClass::NumMoveSize(size), _freeLists[index].MaxSize());
	// �� CentralCache ҪbatchNum���ڴ��
	if (_freeLists[index].MaxSize() == batchNum)
	{
		_freeLists[index].MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr;

	// ����FetchRangeObj������
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

// ����
// ��ThreadCache�����ڴ�ʱ������Ҫ֪����λ���Ǹ�Ͱ����������
// Ȼ������
// �����ͰΪ�գ�����Central��ȡ
// ��֮����ֱ������
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

// �ͷŶ�������������������ڴ浽���Ļ���
void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;

	//��list��ȡ��һ�����������Ķ���
	list.PopRange(start, end, list.MaxSize());

	//��ȡ���Ķ��󻹸�central cache�ж�Ӧ��span
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}

// �ͷ�
// ����Ĳ��� size ���ڴ�����Ĵ�С�����Ǹ���
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);

	// �Ҷ�ӳ�����������Ͱ������������
	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);
	

	// ��Ȼ��Ϊ�˱���ĳ��threadcahceռ��̫���ڴ����
	// ���±��threadcache������״̬
	// ҲҪ����threadcache��centralcache�黹

	// �����������ȴ���һ����������Ķ������ʱ�Ϳ�ʼ��һ��list��central cache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		ListTooLong(_freeLists[index], size);
	}
}
