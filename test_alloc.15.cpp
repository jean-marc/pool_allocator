/*
 *	troubleshoot problem with volatile allocator
 *
 *
 */
#include "pool_allocator.h"
#include <vector>
using namespace std;
typedef vector<int,volatile_allocator_unmanaged<int>> V; 
int main(){
	V v(100);	
	v.resize(50);
	//v.shrink_to_fit();//does not deallocate, actually create a brand new vector of size 50
	//causes long warning message but no error
	//v.erase(v.begin());
	//for(auto i:v){

	//}

}
