#pragma once


#include "common.h"

// ����ģʽ
// һ����ֻ����ʵ����һ��
class CentralCache
{
public:
	// ��������ǵ���ģʽ��ʵ�֣���ȡCentralCache���Ψһʵ��
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}
	// ��ȡһ���ǿյ�span
	Span* GetOneSpan(SpanList& list, size_t size);

	// �����Ļ����ȡһ�������Ķ����thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);

	// ��һ�������Ķ����ͷŵ�span���
	void ReleaseListToSpans(void* start, size_t size);
private:
	// ��Ϊһ��CentralCache������������������飬�����̹߳���CentralCache
	// ��ṹ����һ����ϣͰ�Ľṹ
	SpanList _spanLists[MAX_FREE_LIST];

private:
	CentralCache()
	{
	}

	CentralCache(const CentralCache&) = delete;

	static CentralCache _sInst;
};