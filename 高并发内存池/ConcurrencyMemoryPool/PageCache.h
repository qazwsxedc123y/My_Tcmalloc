#pragma once

#include "Common.h"
#include "ObjectPool.h"
#include "PageMap.h"

// 1.page cache��һ����ҳΪ��λ��span��������
// 2.Ϊ�˱�֤ȫ��ֻ��Ψһ��page cache������౻��Ƴ��˵���ģʽ��
class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}

	// ��ȡһ��Kҳ��span
	Span* NewSpan(size_t k);

	//��ȡ�Ӷ���span��ӳ��
	Span* MapObjectToSpan(void* obj);

	// �ͷſ���span�ص�Pagecache�����ϲ����ڵ�span
	void ReleaseSpanToPageCache(Span* span);

	std::mutex _pageMtx;
private:
	SpanList _spanLists[NPAGES];
	ObjectPool<Span> _spanPool;

	// ֻ���� page cache����span��central cache ��span
	//std::unordered_map<PAGE_ID, Span*> _idSpanMap; // ��һ����ҳ�ţ��ڶ�����span
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;

	PageCache()
	{ }
	PageCache(const PageCache&) = delete;

	static PageCache _sInst;
};