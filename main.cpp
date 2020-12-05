#include <cstdio>
#include "stackvector.h"

unsigned long __stack = 64 * 1024;

class test {
	static size_t _cnt;
	public:
		test() { _val = _cnt ++; };
		~test() { printf("destruct val %ld\n", _val); };
	private:
		size_t _val;
};
size_t test::_cnt = 1;

int main(void)
{
	StackVector<int> stack(10);

	printf("stack is valid: %d\n", stack.isValid());
	
	if (stack.isValid()) {
	
		printf("item 0 at %p\n", &stack[0]);
		
		stack.forEach([](int& member, size_t index) {
			member = index;
		});

		stack.forEach([](int& member, size_t index) {
			printf("member at %d = %d\n", index, member);
		});
	}

	StackVector<int> stack2(500000);

	StackVector<test> stack3(100, 2048);

	return 0;
}
