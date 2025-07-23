#pragma once

#include "Common.h"

class ThreadCache
{
public:
	// ������ͷ��ڴ����
	void* Allocate(size_t size);
	void Deallocate(void* ptr, size_t size);

	// �����Ļ����ȡ����
	// ��ĳ����������Ϊ��ʱ���޷���������Ŀռ䣬����Ҫ��CentralCache��ȡ����
	// Ȼ��ThreadCache�����㹻���ڴ��
	void* FetchFromCentralCache(size_t index, size_t size);

	// �ͷŶ�������������������ڴ浽���Ļ���
	void ListTooLong(FreeList& list, size_t size);

private:
	// ��Ϊһ��ThreadCache������������������飬��Ϊ�����̵߳�����ThreadCache
	// ��ṹ����һ����ϣͰ�Ľṹ
	FreeList _freeLists[MAX_FREE_LIST];
};

// TLS thread local storage
// �䶨����һ�� �ֲ߳̾��洢 (Thread-Local Storage, TLS) �ľ�ָ̬�����
// _declspec(thread)��Windows ƽ̨���еĹؼ���
// ��ʾ�ñ����� �ֲ߳̾��洢��TLS������ÿ���̶߳���ӵ�иñ����Ķ����������̼߳以�����š�
// ThreadCache* pTLSThreadCache
// ����һ��ָ�� ThreadCache ������ָ�������������Ϊ pTLSThreadCache
// ��ʼ��Ϊnullptr
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;