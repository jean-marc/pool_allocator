/*
 *	test std::set
 *
 *
 */
#include "pool_allocator.h"
#include <set>

using namespace std;
int main(){
	typedef set<int,std::less<int>,persistent_allocator_unmanaged<int>> test;
	//typedef set<int,std::less<int>,std::allocator<int>> test;
	test t;
	t.insert(1);
	/*
	typedef persistent_allocator_managed<test> ALLOCATOR;
	ALLOCATOR a;
	auto p=a.allocate(1);
	a.construct(p);	
	p->insert(1);
	p->insert(2);
	p->insert(3);
	*/


}



