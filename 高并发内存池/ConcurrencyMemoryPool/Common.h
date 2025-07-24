#pragma once

#include <iostream>
#include <vector>
#include <algorithm>
#include <unordered_map>

#include <assert.h>
#include <time.h>

#include <mutex>
#include <thread>
using std::cout;
using std::endl;

static const size_t MAX_BYTES = 256 * 1024;
static const size_t MAX_FREE_LIST = 208;
static const size_t NPAGES = 129;
static const size_t PAGE_SHIFT = 13;

#ifdef _WIN32
#include <windows.h>
#else
// ...
#endif

#ifdef _WIN64
	typedef unsigned long long PAGE_ID;
#elif _WIN32
	typedef size_t PAGE_ID;
#endif


// 直接去堆上按页申请空间
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// linux下brk mmap等
#endif

	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}

inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// sbrk unmmap等
#endif
}




// 用函数来获取其下一个内存块
static void*& NextObj(void* obj)
{
	return *(void**)obj;
}

// 管理切分好的小对象的自由链表
class FreeList
{
public:
	void* Pop()
	{
		assert(_freeList);

		// 头删
		void* obj = _freeList;
		_freeList = NextObj(obj);

		--_size;

		return obj;
	}

	void Push(void* obj)
	{
		assert(obj);

		// 头插
		NextObj(obj) = _freeList;
		_freeList = obj;
		++_size;
	}

	void PushRange(void* start, void* end, int n)
	{
		NextObj(end) = _freeList;
		_freeList = start;


		//// 条件断点 + 测试验证
		//int i = 0;
		//void* cur = start;
		//while (cur)
		//{
		//	i++;
		//	cur = NextObj(cur);
		//}
		//if (i != n)
		//{
		//	int x = 0;
		//}

		_size += n;
	}

	void PopRange(void*& start, void*& end, size_t n)
	{
		assert(n <= _size);

		start = _freeList;
		end = start;

		// bug
		// 想要删那么多，但实际没有那么多
		for (size_t i = 0; i < n - 1; ++i)
		{
			end = NextObj(end);
		}

		_freeList = NextObj(end);
		NextObj(end) = nullptr;
		_size -= n;
	}

	// 判断此时这个ThreadCache的自由链表是否为空
	// 他有很多个自由链表，每一个链表都是_freeList对象
	bool Empty()
	{
		return _freeList == nullptr;
	}

	size_t Size()
	{
		return _size;
	}

	size_t& MaxSize()
	{
		return _maxSize;
	}
private:
	void* _freeList = nullptr;
	size_t _maxSize = 1;
	size_t _size = 0; // 表示当前自由链表的节点个数
};

// 准备工作，计算出其相对应的桶的位置 -- ThreadCache
class SizeClass
{
public:
	// 整体控制在最多10%左右的内碎片浪费
	// [1,128]					8byte对齐	    freelist[0,16)
	// [128+1,1024]				16byte对齐	    freelist[16,72)
	// [1024+1,8*1024]			128byte对齐	    freelist[72,128)
	// [8*1024+1,64*1024]		1024byte对齐     freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte对齐   freelist[184,208)

	// 计算该申请的内存片对其后的字节数
	 
	//static inline size_t _RoundUp(size_t size, size_t alignNum)
	//{
	//	size_t alignSize;
	//	if (size % alignNum != 0)
	//	{
	//		alignSize = (size / alignNum + 1) * alignNum;
	//	}
	//	else 
	//	{
	//		alignSize = size;
	//	}
	//	return alignSize;
	//}
	static inline size_t _RoundUp(size_t bytes, size_t alignNum)
	{
		return ((bytes + alignNum - 1) & ~(alignNum - 1));
	}
	static inline size_t RoundUp(size_t size)
	{
		if (size <= 128)
		{
			return _RoundUp(size, 8);
		}
		else if (size <= 1024)
		{
			return _RoundUp(size, 16);
		}
		else if (size <= 8 * 1024)
		{
			return _RoundUp(size, 128);
		}
		else if (size <= 64 * 1024)
		{
			return _RoundUp(size, 1024);
		}
		else if (size <= 256 * 1024)
		{
			return _RoundUp(size, 8 * 1024);
		}
		else
		{
			return _RoundUp(size, 1 << PAGE_SHIFT);
		}
	}

	// 计算映射的哪一个自由链表桶

	//static inline size_t _Index(size_t bytes, size_t alignNum)
	//{
	//	if (bytes % alignNum == 0)
	//	{
	//		return bytes / alignNum - 1;
	//	}
	//	else
	//	{
	//		return bytes / alignNum;
	//	}
	//}

	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		// 每个区间有多少个链
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128)
		{
			// return _Index(bytes, 8);
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024)
		{
			// return _Index(bytes - 128, 16) + group_array[0];
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8 * 1024)
		{
			// return _Index(bytes - 1024, 128) + group_array[0] + group_array[1];
			return _Index(bytes - 1024, 7) + group_array[0] + group_array[1];
		}
		else if (bytes <= 64 * 1024)
		{
			// return _Index(bytes - 8 * 1024, 1024) + group_array[0] + group_array[1] + group_array[2];
			return _Index(bytes - 8 * 1024, 10) + group_array[0] + group_array[1] + group_array[2];
		}
		else if(bytes <= 256 * 1024)
		{
			// return _Index(bytes- 64 * 1024, 8 * 1024) + group_array[0] + group_array[1] + group_array[2] + group_array[3];
			return _Index(bytes - 64 * 1024, 13) + group_array[0] + group_array[1] + group_array[2] + group_array[3];
		}
		else
		{
			assert(false);
		}

		return -1;
	}

	// 一次thread cache从中心缓存获取多少个
	static size_t NumMoveSize(size_t size)
	{
		assert(size <= MAX_BYTES);
		assert(size > 0);


		// [2, 512]，一次批量移动多少个对象的(慢启动)上限值
		// 小对象一次批量上限高
		// 小对象一次批量上限低
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;

		return num;
	}

	// central cache一次向page cache获取多少页
	// 单个对象 8byte
	// ...
	// 单个对象 256KB
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = num * size;

		npage >>= PAGE_SHIFT;
		if (npage == 0)
			npage = 1;

		return npage;
	}
private:
};

// 管理多个连续页大块内存跨度结构
// 管理以页为单位的大块内存
struct Span
{
	PAGE_ID _pageId = 0; // 大块内存起始页的页号
	size_t _n = 0; // 页的数量

	Span* _next = nullptr; // 双向链表的结构
	Span* _prev = nullptr;

	size_t _objSize = 0;  // 切好的小对象的大小
	// 其Span也是由多个小块内存的自由链表组成
	size_t _useCount = 0; // 使用计数，==0 说明所有的对象都还回来了，其值表示被分配给thread cache的计数
	void* _freeList = nullptr;  // 切好的小块内存的自由链表

	bool _isUse = false;          // 是否在被使用
};

// 带头双向循环链表 
// 有哨兵位
class SpanList
{
public:
	SpanList()
	{
		//_head = new Span();
		_head = _spanPool.New();
		_head->_next = _head;
		_head->_prev = _head;
	}

	Span* Begin()
	{
		return _head->_next;
	}
	Span* End()
	{
		return _head;
	}
	bool Empty()
	{
		return _head->_next == _head;
	}

	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}
	Span* PopFront()
	{
		Span* it = _head->_next;
		Erase(it);
		return it;
	}

	void Insert(Span* pos, Span* newSpan)
	{
		// 向pos头插nweSpan
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;
		// prev newspan pos
		prev->_next = newSpan;
		newSpan->_prev = prev;

		newSpan->_next = pos;
		pos->_prev = newSpan;
	}

	void Erase(Span* pos)
	{
		// 删除pos
		assert(pos);
		assert(pos != _head);

		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;
	}
private:
	Span* _head;
	static ObjectPool<Span> _spanPool;
public:
	std::mutex _mtx; // 桶锁
};
