/* Written by Jacek Piszczek. Licensed as Public Domain. Target OS: MorphOS. */
#pragma once
#include <cstdio>
#include <string>
#include <emul/emulregs.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <alloca.h>
#include <functional>

#if defined(DEBUG) && DEBUG
extern "C" { void dprintf(const char *,...); };
#define SVOUT printf 
#else
#define SVOUT(...)
#endif

/* Helper class aiming to streamline creation of temporary vectors for OBArray object iterations 
** in ObjectiveC++ applications, but may have other uses too. The memory for the vector is allocated
** either on stack (if there's enough of it to spare AND the object itself was also allocated
** on stack) or heap. */

template <typename T> class StackVector
{
public:
	/* MUST be inlined or alloca() would fail to survive past this method */
	__attribute__((always_inline)) StackVector(const size_t size, const size_t mustLeaveStackSizeForScope = (16 * 1024), bool callConstructorsDestructors = true)
		: _size(size), _callFree(false), _callConstructorsDestructors(callConstructorsDestructors)
	{
		const size_t needBytes = size * sizeof(T);
		bool onStack = canReserveStack(needBytes, mustLeaveStackSizeForScope) ;

		if (onStack) {
#if defined(DEBUG) && DEBUG
			struct Task *t = FindTask(NULL);
			ULONG usedStack = 0, usedStackAfterAlloca = 0;
			NewGetTaskAttrsA(t, &usedStack, sizeof (usedStack), TASKINFOTYPE_USEDSTACKSIZE, NULL);
			_memory = static_cast<T*>(alloca(needBytes));
			NewGetTaskAttrsA(t, &usedStackAfterAlloca, sizeof (usedStackAfterAlloca), TASKINFOTYPE_USEDSTACKSIZE, NULL);
			SVOUT("%s: allocated on stack %p, alloca using stack? %d stack usage grew by %d\n", __PRETTY_FUNCTION__, _memory, isAllocatedOnStack(), usedStackAfterAlloca - usedStack);
#else
			_memory = static_cast<T*>(alloca(needBytes));
#endif
		}
		else {
			_memory = static_cast<T*>(malloc(needBytes));
			_callFree = true;
			SVOUT("%s: allocated on heap %p\n", __PRETTY_FUNCTION__, _memory);
		}
		
		if (_callConstructorsDestructors && _memory) {
			for (size_t i = 0; i < size; i++) {
				new (&_memory[i]) T ();
			}
		}
	}
	
	StackVector() = delete;
	
	~StackVector()
	{
		if (_callConstructorsDestructors && _memory) {
			for (size_t i = 0; i < _size; i++) {
				(&_memory[i])->~T();
			}
		}

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

	// Iterates over the vector using a lambda
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

protected:
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
	bool     _callFree : 1;
	bool     _callConstructorsDestructors : 1;
};

#ifdef __OBJC__

#import <ob/OBArray.h>

class IDVector : public StackVector<id>
{
public:
	IDVector(size_t size) : StackVector<id>(size, 32 * 1024, false) { };
};

/*
** GCC doesn't really compile fast enumeration in ObjectiveC++ files, so this can be used to replace it.
** Example:
**  BOOL found = NO;
** 	FastEnumerator<OBString*> objects(stringarray,[&found](OBString* &string, size_t index) {
**    if ([string isEqualToString:@"string we're looking for"]) {
**       found = YES; return false;
**    }
**    return true; // keep going
**  });
*/

template <typename O> class FastEnumerator : protected StackVector<O> 
{
public:
	FastEnumerator(OBArray *arrayToEnumerate, std::function<bool(O& member, size_t index)> && enumCallback) : StackVector<O>([arrayToEnumerate count], 32 * 1024, false) {
		if (StackVector<O>::_memory) {
			[arrayToEnumerate getObjects:StackVector<O>::_memory];
			StackVector<O>::whileEach(std::move(enumCallback));
		}
	};
	FastEnumerator() = delete;
	~FastEnumerator() = default;
};

#endif
