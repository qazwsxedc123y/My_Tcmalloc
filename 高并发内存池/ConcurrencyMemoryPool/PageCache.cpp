#define  _CRT_SECURE_NO_WARNINGS

#include "PageCache.h"
PageCache PageCache::_sInst;

// ��ȡһ��Kҳ��span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);

	// ���� 128 ҳֱ�����������
	if (k > NPAGES - 1)
	{
		void* ptr = SystemAlloc(k);

		// Span* span = new Span;
		Span* span = _spanPool.New();

		span->_n = k;
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;

		//_idSpanMap[span->_pageId] = span;
		_idSpanMap.set(span->_pageId, span);
		return span;
	}

	if (!_spanLists[k].Empty())
	{
		Span* kSpan = _spanLists[k].PopFront();

		//����ҳ����span��ӳ�䣬����central cache����С���ڴ�ʱ���Ҷ�Ӧ��span
		for (size_t i = 0; i < kSpan->_n; i++)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}

		return kSpan;
	}

	// �ߵ�����˵����ǰpagecacheû��kҳ��span
	// ���һ�º����Ͱ������û��span������п��԰����������з�
	for (size_t i = k + 1; i < NPAGES; i++)
	{
		if (!_spanLists[i].Empty())
		{
			// �����з֣��з�Ϊkҳ �� i - kҳ��span
			// �з����Ҫ���뵽��Ӧ��pagecahceλ��
			Span* nSpan = _spanLists[i].PopFront();
			// Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			// ��nSpan��ͷ����һ��kҳ����
			// kҳspan����
			// nSpan�ٹҵ���Ӧӳ���λ��
			kSpan->_n = k;
			kSpan->_pageId = nSpan->_pageId;

			nSpan->_n -= k;
			nSpan->_pageId += k;

			_spanLists[nSpan->_n].PushFront(nSpan);

			// �洢nSpan����λҳ�Ÿ�nSpanӳ�䣬����page cache�����ڴ�ʱ
			// ���еĺϲ�����
			//_idSpanMap[nSpan->_pageId] = nSpan;
			//_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
			_idSpanMap.set(nSpan->_pageId, nSpan);
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);

			//����ҳ����span��ӳ�䣬����central cache����С���ڴ�ʱ���Ҷ�Ӧ��span
			for (PAGE_ID i = 0; i < kSpan->_n; i++)
			{
				//_idSpanMap[kSpan->_pageId + i] = kSpan;
				_idSpanMap.set(kSpan->_pageId + i, kSpan);
			}

			return kSpan;
		}
	}

	// �ߵ����λ�þ�˵������û�д�ҳ��span��
	// ��ʱ��ȥ�Ҷ�Ҫһ��128ҳ��span

	// Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;

	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);
}

// ��ȡ�Ӷ���span��ӳ��
// ҳ��ӳ��span
Span* PageCache::MapObjectToSpan(void* obj)
{
	// �� threadcache �� centralcache ���������ڴ��
	// �ȼ����ҳ��
	// ���ù�ϣӳ���span
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT; // ҳ��

	//std::unique_lock<std::mutex> lock(_pageMtx); //����ʱ����������ʱ�Զ�����
	//auto it = _idSpanMap.find(id);
	//if (it != _idSpanMap.end())
	//{
	//	return it->second;
	//}
	//else
	//{
	//	assert(false);
	//	return nullptr;
	//}

	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;
}


void PageCache::ReleaseSpanToPageCache(Span* span)
{
	// ����128 page��ֱ�ӻ�����
	if (span->_n > NPAGES - 1)
	{
		void* ptr = (void*)(span->_pageId >> PAGE_SHIFT);
		SystemFree(ptr);
		// delete span;
		_spanPool.Delete(span);

		return;
	}


	// ��spanǰ���ҳ�����Խ��кϲ��������ڴ���Ƭ����
	
	// �ȳ�����ǰ���ҳ���кϲ�
	while (1)
	{
		PAGE_ID prevId = span->_pageId - 1;
		//auto previt = _idSpanMap.find(prevId);
		//// ǰ���ҳ��û�У����ϲ���
		//if (previt == _idSpanMap.end())
		//{
		//	break;
		//}

		auto ret = (Span*)_idSpanMap.get(prevId);
		if (ret == nullptr)
		{
			break;
		}
		// ǰ������ҳ��span��ʹ�ã����ϲ���
		//Span* prevSpan = previt->second;
		Span* prevSpan = ret;
		if (prevSpan->_isUse == true)
		{
			break;
		}

		// �ϲ�������128ҳ��spanû�취�������ϲ���
		if (prevSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		// ��ʱ���кϲ�
		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);

		// delete prevSpan;
		_spanPool.Delete(prevSpan);
	}

	// ���ϲ�
	while (1)
	{
		PAGE_ID nextId = span->_pageId + span->_n;
		//auto nextit = _idSpanMap.find(nextId);
		//// ǰ���ҳ��û�У����ϲ���
		//if (nextit == _idSpanMap.end())
		//{
		//	break;
		//}

		auto ret = (Span*)_idSpanMap.get(nextId);
		if (ret == nullptr)
		{
			break;
		}

		// ǰ������ҳ��span��ʹ�ã����ϲ���
		//Span* nextSpan = nextit->second;
		Span* nextSpan = ret;
		if (nextSpan->_isUse == true)
		{
			break;
		}

		// �ϲ�������128ҳ��spanû�취�������ϲ���
		if (nextSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		// ��ʱ���кϲ�

		span->_n += nextSpan->_n;

		_spanLists[nextSpan->_n].Erase(nextSpan);

		// delete nextSpan;
		_spanPool.Delete(nextSpan);
	}

	// ��ʱ�������˺ϲ�
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	//_idSpanMap[span->_pageId] = span;
	//_idSpanMap[span->_pageId + span->_n - 1] = span;

	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
}