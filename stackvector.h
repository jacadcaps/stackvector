#pragma once
#include <cstdio>
#include <string>
#include <emul/emulregs.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <alloca.h>
#include <functional>

#define STACKVECTORDEBUG
#ifdef STACKVECTORDEBUG
extern "C" { void dprintf(const char *,...); };
#define SVOUT printf 
#endif

template <typename T> class StackVector
{
public:
	__attribute__((always_inline)) StackVector(const size_t size, const size_t mustLeaveStackSizeForScope = (16 * 1024))
		: _size(size), _callFree(false)
	{
		const size_t needBytes = size * sizeof(T);
		bool onStack = canReserveStack(needBytes, mustLeaveStackSizeForScope) ;

		if (onStack)
		{
			_memory = static_cast<T*>(alloca(needBytes));
			SVOUT("%s: allocated on stack %p, confirmed %d\n", __PRETTY_FUNCTION__, _memory, isAllocatedOnStack());
		}
		else
		{
			_memory = static_cast<T*>(malloc(needBytes));
			_callFree = true;
			SVOUT("%s: allocated on heap %p\n", __PRETTY_FUNCTION__, _memory);
		}
	}
	
	~StackVector()
	{
		if (_callFree)
		{
			SVOUT("%s: freeing heap %p..\n", __PRETTY_FUNCTION__, _memory);
			free(_memory);
		}
		else
		{
			SVOUT("%s: memory was alloca'd\n", __PRETTY_FUNCTION__);
		}
	}

	size_t count() const { return _size; }
	bool isValid() const { return _memory != nullptr && _size > 0; }

	// Invalid when called from another thread than the one that constructed the object
	bool isAllocatedOnStack() const { return isStackAddress(FindTask(0), _memory); }
	
	void forEach(std::function<void(T& member, size_t index)>&& onEach) {
		if (_memory) {
			for (size_t idx = 0; idx < _size; idx++) {
				onEach(_memory[idx], idx);
			}
		}
	}
	
	void forEach(std::function<void(const T& member, size_t index)>&& onEach) const {
		if (_memory) {
			for (size_t idx = 0; idx < _size; idx++) {
				onEach(_memory[idx], idx);
			}
		}
	}

	void whileEach(std::function<bool(T& member, size_t index)>&& onEach) {
		if (_memory) {
			for (size_t idx = 0; idx < _size; idx++) {
				if (!onEach(_memory[idx], idx))
					break;
			}
		}
	}

	void whileEach(std::function<bool(const T& member, size_t index)>&& onEach) const {
		if (_memory) {
			for (size_t idx = 0; idx < _size; idx++) {
				if (!onEach(_memory[idx], idx))
					break;
			}
		}
	}

	T& operator[](size_t index) {
#ifdef STACKVECTORDEBUG
		if (index >= _size)
		{
			SVOUT("%s: Access at %d outside of size %d\n", __PRETTY_FUNCTION__, index, _size);
		}
#endif
		return _memory[index];
	}
	
	T const & operator[] (size_t index) const {
#ifdef STACKVECTORDEBUG
		if (index >= _size)
		{
			SVOUT("%s: Access at %d outside of size %d\n", __PRETTY_FUNCTION__, index, _size);
		}
#endif
		return _memory[index];
	}

private:
	bool canReserveStack(const size_t size, const size_t mustLeaveStackSizeForScope) const
	{
		struct Task *t = FindTask(NULL);
		if (isStackAddress(t, const_cast<StackVector<T>*>(this)))
		{
			ULONG usedStack = 0;
			if (0 != NewGetTaskAttrsA(t, &usedStack, sizeof (usedStack), TASKINFOTYPE_USEDSTACKSIZE, NULL))
			{
				struct ETask *e = t->tc_ETask;
				ULONG lower = ULONG(e->PPCSPLower);
				ULONG current = ULONG(e->PPCSPUpper) - usedStack;
			
				SVOUT("%s: 'this' was allocated on stack; lower %p current %p current-size %p\n", __PRETTY_FUNCTION__, lower, current, current - size);
			
				if ((lower + mustLeaveStackSizeForScope) < (current - size))
					return true;
			}
		}

		return false;
	}
	
	bool isStackAddress(struct Task *t, void *address) const
	{
		struct ETask *e = t->tc_ETask;
		SVOUT("%s: lower %p upper %p addr %p \n", __PRETTY_FUNCTION__, e->PPCSPLower, e->PPCSPUpper, address);
		return (address > e->PPCSPLower) && (address < e->PPCSPUpper);
	}
	
	T       *_memory;
	size_t   _size;
	bool     _callFree;
};
