#define  _CRT_SECURE_NO_WARNINGS

#include "PageCache.h"
PageCache PageCache::_sInst;

// 获取一个K页的span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);

	// 大于 128 页直接向堆上申请
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

		//建立页号与span的映射，方便central cache回收小块内存时查找对应的span
		for (size_t i = 0; i < kSpan->_n; i++)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}

		return kSpan;
	}

	// 走到这里说明当前pagecache没有k页的span
	// 检查一下后面的桶里面有没有span，如果有可以把他它进行切分
	for (size_t i = k + 1; i < NPAGES; i++)
	{
		if (!_spanLists[i].Empty())
		{
			// 进行切分，切分为k页 与 i - k页的span
			// 切分完后，要插入到对应的pagecahce位置
			Span* nSpan = _spanLists[i].PopFront();
			// Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			// 在nSpan的头部切一个k页下来
			// k页span返回
			// nSpan再挂到对应映射的位置
			kSpan->_n = k;
			kSpan->_pageId = nSpan->_pageId;

			nSpan->_n -= k;
			nSpan->_pageId += k;

			_spanLists[nSpan->_n].PushFront(nSpan);

			// 存储nSpan的首位页号跟nSpan映射，方便page cache回收内存时
			// 进行的合并查找
			//_idSpanMap[nSpan->_pageId] = nSpan;
			//_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
			_idSpanMap.set(nSpan->_pageId, nSpan);
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);

			//建立页号与span的映射，方便central cache回收小块内存时查找对应的span
			for (PAGE_ID i = 0; i < kSpan->_n; i++)
			{
				//_idSpanMap[kSpan->_pageId + i] = kSpan;
				_idSpanMap.set(kSpan->_pageId + i, kSpan);
			}

			return kSpan;
		}
	}

	// 走到这个位置就说明后面没有大页的span了
	// 这时就去找堆要一个128页的span

	// Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;

	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);
}

// 获取从对象到span的映射
// 页号映射span
Span* PageCache::MapObjectToSpan(void* obj)
{
	// 由 threadcache 向 centralcache 还回来的内存块
	// 先计算出页号
	// 利用哈希映射出span
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT; // 页号

	//std::unique_lock<std::mutex> lock(_pageMtx); //构造时加锁，析构时自动解锁
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
	// 大于128 page的直接还给堆
	if (span->_n > NPAGES - 1)
	{
		void* ptr = (void*)(span->_pageId >> PAGE_SHIFT);
		SystemFree(ptr);
		// delete span;
		_spanPool.Delete(span);

		return;
	}


	// 对span前后的页，尝试进行合并，缓解内存碎片问题
	
	// 先尝试与前面的页进行合并
	while (1)
	{
		PAGE_ID prevId = span->_pageId - 1;
		//auto previt = _idSpanMap.find(prevId);
		//// 前面的页号没有，不合并了
		//if (previt == _idSpanMap.end())
		//{
		//	break;
		//}

		auto ret = (Span*)_idSpanMap.get(prevId);
		if (ret == nullptr)
		{
			break;
		}
		// 前面相邻页的span在使用，不合并了
		//Span* prevSpan = previt->second;
		Span* prevSpan = ret;
		if (prevSpan->_isUse == true)
		{
			break;
		}

		// 合并出超过128页的span没办法管理，不合并了
		if (prevSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		// 此时进行合并
		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);

		// delete prevSpan;
		_spanPool.Delete(prevSpan);
	}

	// 向后合并
	while (1)
	{
		PAGE_ID nextId = span->_pageId + span->_n;
		//auto nextit = _idSpanMap.find(nextId);
		//// 前面的页号没有，不合并了
		//if (nextit == _idSpanMap.end())
		//{
		//	break;
		//}

		auto ret = (Span*)_idSpanMap.get(nextId);
		if (ret == nullptr)
		{
			break;
		}

		// 前面相邻页的span在使用，不合并了
		//Span* nextSpan = nextit->second;
		Span* nextSpan = ret;
		if (nextSpan->_isUse == true)
		{
			break;
		}

		// 合并出超过128页的span没办法管理，不合并了
		if (nextSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		// 此时进行合并

		span->_n += nextSpan->_n;

		_spanLists[nextSpan->_n].Erase(nextSpan);

		// delete nextSpan;
		_spanPool.Delete(nextSpan);
	}

	// 此时进行完了合并
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	//_idSpanMap[span->_pageId] = span;
	//_idSpanMap[span->_pageId + span->_n - 1] = span;

	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
}