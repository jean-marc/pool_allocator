#ifndef SPECIAL_ALLOCATOR_H
#define SPECIAL_ALLOCATOR_H
/*
 *	simplest allocator that works
 *
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <typeinfo>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <functional>
#include <algorithm>
#include <stdexcept>
#include "ifthenelse.hpp"
using namespace  std;
template<typename INDEX> struct _info{
	INDEX size;//size of current range in multiple of sizeof(info)
	INDEX next;//index of next available range 0 means no more range
};
template<> struct _info<void>{
};

template<typename A,typename B,bool test=(sizeof(A)>sizeof(B))> struct pseudo_union{
	A a;
	A& get_a(){return a;}
	B& get_b(){return *(B*)(void*)this;}
};
template<typename A,typename B> struct pseudo_union<A,B,false>{
	B b;
	B& get_a(){return *(A*)(void*)this;}
	A& get_b(){return b;}
};

template<
	typename _INDEX_,	/* the type used as index */
	typename _PAYLOAD_, 	/* the actual payload */
	typename _ALLOCATOR_,	/* where the pool instance will be allocated, distinct from where the buffer is allocated */
	typename _RAW_ALLOCATOR_=std::allocator<char>,	/* where the buffer is allocated */
	typename _MANAGEMENT_=void,	/* overhead to tag allocated cells and do reference counting */
	typename _INFO_=_info<_INDEX_>
> struct cell{
	typedef _INDEX_ INDEX;
	typedef _PAYLOAD_ PAYLOAD;
	typedef _ALLOCATOR_ ALLOCATOR;
	typedef _RAW_ALLOCATOR_ RAW_ALLOCATOR;
	typedef _MANAGEMENT_ MANAGEMENT;
	typedef _INFO_ INFO;
	MANAGEMENT management; //store metadata, not visible to payload, can also be used to detect unauthorized access, maybe would make more sense as first member
	union{
		INFO info;
		char payload[sizeof(PAYLOAD)];//will affect data alignment, we have to investigate the effect of misalignment (extra CPU + exception for special operations)
		//PAYLOAD payload;
	}body;
	typedef cell<INDEX,PAYLOAD,ALLOCATOR,_RAW_ALLOCATOR_,MANAGEMENT,_info<void>> HELPER;	
	enum{MANAGED=true};
	enum{OPTIMIZATION=false};
	enum{FACTOR=1};
	enum{MAX_SIZE=(1L<<(sizeof(INDEX)<<3))-1};//-1 because cell[0] is used for management
	static void post_allocate(cell* begin,cell* end){
		for(cell* i=begin;i<end;++i) i->management=0xf;	
	}
	static void post_deallocate(cell* begin,cell* end){
		for(cell* i=begin;i<end;++i) i->management=0x0;	
	}
	//check if the cell has been allocated
	static void check(cell& c){
		if(!c.management) throw std::out_of_range("bad reference");	
	}
};
template<
	typename _INDEX_,
	typename _PAYLOAD_,
	typename _ALLOCATOR_,//for struct pool{}
	typename _RAW_ALLOCATOR_,
	typename _INFO_
> struct cell<_INDEX_,_PAYLOAD_,_ALLOCATOR_,_RAW_ALLOCATOR_,void,_INFO_>{
	typedef _INDEX_ INDEX;
	typedef _PAYLOAD_ PAYLOAD;
	typedef _ALLOCATOR_ ALLOCATOR;
	typedef void MANAGEMENT;
	typedef _INFO_ INFO;
	typedef _RAW_ALLOCATOR_ RAW_ALLOCATOR;
	union{
		INFO info;
		char payload[sizeof(PAYLOAD)];//problem with data alignment, the members of PAYLOAD are no longer accessible
	}body;
	typedef cell<INDEX,PAYLOAD,ALLOCATOR,_RAW_ALLOCATOR_,void,_info<void>> HELPER;	
	enum{MANAGED=false};
	enum{OPTIMIZATION=(sizeof(INFO)>sizeof(PAYLOAD))&&(sizeof(INFO)%sizeof(PAYLOAD)==0)};
	//enum{OPTIMIZATION=false};
	enum{FACTOR=OPTIMIZATION ? sizeof(INFO)/sizeof(PAYLOAD) : 1};
	//need to take FACTOR into account
	enum{MAX_SIZE=(1L<<(sizeof(INDEX)<<3))/FACTOR-1};
	static void post_allocate(cell*,cell*){}
	static void post_deallocate(cell*,cell*){}
	static void check(cell& c){}
};
struct pool{
	template<
		typename INDEX,
		typename VALUE_TYPE,	
		typename ALLOCATOR,
		typename RAW_ALLOCATOR,
		typename MANAGEMENT
	> struct ptr{
		typedef cell<INDEX,VALUE_TYPE,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
		INDEX index;
		typedef ptr pointer;
		typedef VALUE_TYPE value_type;
		typedef VALUE_TYPE& reference;
		typedef ptrdiff_t difference_type;
		typedef random_access_iterator_tag iterator_category;
		ptr(INDEX index=0):index(index){}
		value_type* operator->()const{
			typedef typename IfThenElse<CELL::OPTIMIZATION,typename CELL::HELPER,CELL>::ResultT PAYLOAD_CELL;
			return &pool::get_pool<CELL>()->get_payload<PAYLOAD_CELL>(index);
		}
		reference operator*()const{
			typedef typename IfThenElse<CELL::OPTIMIZATION,typename CELL::HELPER,CELL>::ResultT PAYLOAD_CELL;
			return pool::get_pool<CELL>()->get_payload<PAYLOAD_CELL>(index);
		}
		ptr& operator+=(INDEX s){index+=s;return *this;}
		ptr& operator++(){++index;return *this;}
		ptr& operator-=(INDEX s){index-=s;return *this;}
		ptr& operator--(){--index;return *this;}
		bool operator==(const ptr& a)const{return index==a.index;}
		bool operator!=(const ptr& a)const{return index!=a.index;}
		bool operator<(const ptr& a)const{return index<a.index;}
		bool operator>(const ptr& a)const{return index>a.index;}
		operator value_type*() {return index ? operator->():0;}
		void _print(ostream& os)const{}
	};

	/*
 	* 	iterators to visit pool in a generic way, we need to skip non-allocated ranges
 	*	end iterator is tricky because we don't know where the last cell is, because they are
 	*	allocated at random positions, we could keep track of last cell but it would take extra work
 	*	at each allocation/deallocation
 	*	it is very similar to ptr_d, maybe it could inherit from it, the problem is to access the MANAGEMENT member
 	*	to test if the cell is being used
 	*/ 
	template<
		typename INDEX,
		typename PAYLOAD,
		typename ALLOCATOR,
		typename RAW_ALLOCATOR,
		typename MANAGEMENT
	>struct cell_iterator{
		typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
		//typedef typename CELL::INDEX INDEX;
		typedef cell_iterator pointer;
		typedef CELL value_type;
		typedef CELL& reference;
		typedef ptrdiff_t difference_type;
		typedef forward_iterator_tag iterator_category;
		INDEX index;
		INDEX n;//number of cells to visit
		cell_iterator(INDEX index=0,INDEX n=pool::get_pool<CELL>()->get_cells<CELL>()[0].body.info.size):index(index),n(n){
			//while(!(*this)->management&&n) ++index;
			while(!pool::get_pool<CELL>()->get_cells<CELL>()[index].management&&n){
				//cerr<<operator->()->management<<"\t"<<pool::get_pool<CELL>()->get_cells<CELL>()[index].management<<"\t"<<index<<"\t"<<n<<endl;
				 ++index;
			}
		}
		cell_iterator& operator++(){
			++index;
			//while(!(*this)->management&&n) ++index;
			while(!pool::get_pool<CELL>()->get_cells<CELL>()[index].management&&n){
				//cerr<<pool::get_pool<CELL>()->get_cells<CELL>()[index].management<<"\t"<<index<<"\t"<<n<<endl;
				 ++index;
			}
			--n;
			return *this;
		}
		value_type* operator->()const{return &pool::get_pool<CELL>()->get_cells<CELL>()[index];}
		reference operator*()const{return pool::get_pool<CELL>()->get_cells<CELL>()[index];}
		bool operator==(const cell_iterator& a)const{return n==a.n;}
		bool operator!=(const cell_iterator& a)const{return n!=a.n;}
	};
	/*
 	*	allocate memory on file
 	*/ 
	struct mmap_allocator_impl{
		int fd;
		bool writable;
		void* v;
		size_t file_size;
		enum{PAGE_SIZE=4096};
		mmap_allocator_impl(string filename):writable(true){
			cerr<<"opening file `"<<filename<<"' O_RDWR"<<endl;
			fd = open(filename.c_str(), O_RDWR | O_CREAT/* | O_TRUNC*/, (mode_t)0600);
			if(fd ==-1){
				cerr<<"opening file `"<<filename<<"' O_RDONLY"<<endl;
				fd = open(filename.c_str(), O_RDONLY/* | O_TRUNC*/, (mode_t)0600);
				writable=false;
			}
			if (fd == -1) {
				cerr<<"\nError opening file `"<<filename<<"' for writing or reading"<<endl;
				exit(EXIT_FAILURE);
			}
			//set the size
			struct stat s;
			int r=fstat(fd,&s);
			if(r==-1){
				cerr<<"\ncould not stat file `"<<filename<<"'"<<endl;
				exit(EXIT_FAILURE);
			}
			file_size=0;
			if(s.st_size==0){
				//new file
				file_size=PAGE_SIZE;
				int result = lseek(fd,file_size-1, SEEK_SET);
				if (result == -1) {
					close(fd);
					cerr<<"Error calling lseek() to 'stretch' the file"<<endl;
					exit(EXIT_FAILURE);
				}
				result = write(fd, "", 1);
				if (result != 1) {
					close(fd);
					cerr<<"Error writing last byte of the file"<<endl;
					exit(EXIT_FAILURE);
				}
			}else{
				file_size=s.st_size;
			}
			v = writable ? mmap((void*)NULL,file_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0) : mmap((void*)NULL,file_size,PROT_READ,MAP_SHARED,fd,0);
			cerr<<"new mapping at "<<v<<" size:"<<file_size<<endl;
			if (v == MAP_FAILED) {
				close(fd);
				cerr<<"Error mmapping the file"<<endl;
				exit(EXIT_FAILURE);
			}
		}
		//it is not a proper allocator
		char* allocate(size_t n){
			if(n>file_size){
				size_t _file_size=max<size_t>(ceil((double)(n)/PAGE_SIZE),1)*PAGE_SIZE;
				int result = lseek(fd,_file_size-1, SEEK_SET);
				if (result == -1) {
					close(fd);
					cerr<<"Error calling lseek() to 'stretch' the file"<<endl;
					exit(EXIT_FAILURE);
				}
				result = write(fd, "", 1);
				if (result != 1) {
					close(fd);
					cerr<<"Error writing last byte of the file"<<endl;
					exit(EXIT_FAILURE);
				}
				void* _v=(char*)mremap(v,file_size,_file_size,MAP_SHARED,MREMAP_MAYMOVE);
				if (_v == MAP_FAILED) {
					close(fd);
					cerr<<"Error mremapping the file"<<endl;
					exit(EXIT_FAILURE);
				}
				v=_v;
				cerr<<"new mapping at "<<v<<" size:"<<_file_size<<endl;
				file_size=_file_size;
			}
			cerr<<"mmap_allocator::allocate "<<v<<endl;
			return (char*)v;
		}
	};
	//confusing T is not the type allocated, just a unique type used for identification
	template<typename T> struct mmap_allocator{
		typedef char* pointer;
		static mmap_allocator_impl* get_impl(){
			/* could also use the hash of the typeid in case names are too long or not valid file names 
			 * symbolic links could be used to provide meaningful names
			 */
			static mmap_allocator_impl* a=new mmap_allocator_impl(string("db/")+typeid(T).name());
			return a;
		}
		//we know that there will be only one range used at any given time
		pointer allocate(size_t n){
			return get_impl()->allocate(n);
		}
		void deallocate(pointer p,size_t n){

		}
	};
	template<
		typename INDEX,
		typename PAYLOAD,
		typename ALLOCATOR,
		typename RAW_ALLOCATOR,
		typename MANAGEMENT
	> struct allocator{
		typedef ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> pointer;
		typedef typename pointer::value_type value_type;
		typedef typename pointer::reference reference;
		//we have to decide if const pointers use different pool
		typedef	pointer const_pointer; 
		typedef typename const_pointer::reference const_reference;
		typedef std::size_t size_type;
		typedef std::ptrdiff_t difference_type;
		allocator(){}
		~allocator(){}
		//this is a upper boundary of the maximum size 
		size_type max_size() const throw(){
			typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
			return CELL::MAX_SIZE;
		}
		pointer allocate(size_type n){
			typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
			return pointer(pool::get_pool<CELL>()->template allocate<CELL>(max<size_t>(n/CELL::FACTOR,1))*CELL::FACTOR);
		}
		pointer allocate_at(INDEX i,size_type n){
			typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
			return pointer(pool::get_pool<CELL>()->template allocate_at<CELL>(i,max<size_t>(n/CELL::FACTOR,1))*CELL::FACTOR);
		}
		void deallocate(pointer p,size_type n){
			typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
			pool::get_pool<CELL>()->template deallocate<CELL>(p.index/CELL::FACTOR,max<size_t>(n/CELL::FACTOR,1));
		}
		template<class OTHER_PAYLOAD> struct rebind{
			typedef allocator<INDEX,OTHER_PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> other;
		};
		static typename ALLOCATOR::pointer get_pool(){
			typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
			return pool::get_pool<CELL>();
		}
		template<typename... Args> void construct(pointer p,Args... args){
			cerr<<"construct at "<<(int)p.index<<"("<<(void*)p.operator->()<<")"<<endl;
			new(p) value_type(args...);
		}
		void destroy(pointer p){
			cerr<<"destroy at "<<(int)p.index<<endl;
			p->~value_type();
		}
 		//this might cause problem, is the pointer still valid? it should be because when using vector a whole block is allocated
		template<typename... Args> void construct(value_type* p,Args... args){
			cerr<<"construct at "<<(void*)p<<endl;
			new(p) value_type(args...);
		}
		void destroy(value_type* p){
			cerr<<"destroy at "<<(void*)p<<endl;
			p->~value_type();
		}
		//typed iterator
		//typedef cell_iterator<CELL> iterator;
		typedef cell_iterator<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> iterator;
		typedef iterator const_iterator;
		iterator begin(){return iterator(1);}
		iterator end(){return iterator(1,0);}
		const_iterator cbegin(){return const_iterator(1);}
		const_iterator cend(){return const_iterator(1,0);}
		//helper function
		template<typename... Args> static pointer construct_allocate(Args... args){
			allocator a;
			auto p=a.allocate(1);	
			a.construct(p,args...);
			return p;
		}
		template<typename... Args> static pointer construct_allocate_at(INDEX i,Args... args){
			allocator a;
			auto p=a.allocate_at(i,1);	
			a.construct(p,args...);
			return p;
		}
		static int _index(){}

	};
	//let's store pools in a pool...maximum 255 pools for now
	//typedef cell<uint8_t,pool,std::allocator<pool>,std::allocator<char>,char> POOL_CELL;
	typedef cell<uint8_t,pool,std::allocator<pool>,mmap_allocator<pool>,char> POOL_CELL;
	//typedef allocator<POOL_CELL> POOL_ALLOCATOR;
	typedef allocator<uint8_t,pool,std::allocator<pool>,mmap_allocator<pool>,char> POOL_ALLOCATOR;
	char* buffer;
	size_t buffer_size;//in byte
	const size_t cell_size;//in byte
	const size_t payload_offset;
	const size_t type_id;//
	/*const*/bool iterable;
	bool writable;
	/*
 	*	shall we store the number of cells in pool instead of the buffer (c[0].body.info.size)
 	*	the only problem is if we save the pool but not the buffer, this could happen?
 	*	that would make it available to generic iterators, alternatively we could have a function pointer
 	*
 	*/ 
	typedef size_t (*f_ptr)(pool&);
	f_ptr get_size_generic;
	template<typename CELL> static size_t get_size(pool& p){return p.get_cells<CELL>()[0].body.info.size;}
	template<typename CELL,typename ALLOCATOR=typename CELL::ALLOCATOR> struct helper{
		static typename CELL::ALLOCATOR::pointer go(){
			typename CELL::RAW_ALLOCATOR raw;
			size_t buffer_size=64*sizeof(CELL);
			auto buffer=raw.allocate(buffer_size);//what about allocating CELL's instead of char?, it would have the advantage of aligning the data
			CELL *c=(CELL*)buffer;
			if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value) memset(buffer,0,buffer_size);
			if(c[0].body.info.size==0&&c[0].body.info.next==0){
				c[0].body.info.size=0;//new pool
				c[0].body.info.next=1;
				c[1].body.info.size=buffer_size/sizeof(CELL)-1;
				c[1].body.info.next=0;
			}
			std::hash<std::string> str_hash;
			typename CELL::ALLOCATOR a;
			auto p=a.allocate(1);
			f_ptr f=pool::get_size<CELL>;
			a.construct(p,buffer,buffer_size,sizeof(CELL),offsetof(CELL,body),str_hash(typeid(typename CELL::PAYLOAD).name()),f/*pool::get_size<CELL>*/);
			return p;
		}
	};
	template<typename CELL> struct helper<CELL,POOL_ALLOCATOR>{
		static typename CELL::ALLOCATOR::pointer go(){
			/*
 			*	maybe the pool has been persisted 
 			*/ 
			std::hash<std::string> str_hash;
			size_t type_id=str_hash(typeid(typename CELL::PAYLOAD).name());
			typename CELL::ALLOCATOR a;
			cerr<<"looking for pool `"<<typeid(typename CELL::PAYLOAD).name()<<"'"<<endl;
			auto i=std::find_if(a.begin(),a.end(),test(type_id));
			if(i==a.end()){
				cerr<<"not found"<<endl;
				typename CELL::RAW_ALLOCATOR raw;
				size_t buffer_size=64*sizeof(CELL);
				auto buffer=raw.allocate(buffer_size);
				if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value) memset(buffer,0,buffer_size);
				CELL *c=(CELL*)buffer;
				if(c[0].body.info.size==0&&c[0].body.info.next==0){
					c[0].body.info.size=0;//new pool
					c[0].body.info.next=1;
					c[1].body.info.size=buffer_size/sizeof(CELL)-1;
					c[1].body.info.next=0;
				}
				auto p=a.allocate(1);
				f_ptr f=pool::get_size<CELL>;
				a.construct(p,buffer,buffer_size,sizeof(CELL),offsetof(CELL,body),type_id,f/*pool::get_size<CELL>*/);
				return p;
			}else{
				typename CELL::RAW_ALLOCATOR raw;
				size_t buffer_size=64*sizeof(CELL);
				auto buffer=raw.allocate(buffer_size);
				if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value) memset(buffer,0,buffer_size);
				CELL *c=(CELL*)buffer;
				if(c[0].body.info.size==0&&c[0].body.info.next==0){
					c[0].body.info.size=0;//new pool
					c[0].body.info.next=1;
					c[1].body.info.size=buffer_size/sizeof(CELL)-1;
					c[1].body.info.next=0;
				}
				//we need a pointer to the pool	
				typename CELL::ALLOCATOR::pointer p(i.index);
				//we only have to refresh the buffer and function pointers
				p->buffer=buffer;
				p->get_size_generic=pool::get_size<CELL>;
				return p;
			}
		}
	};
	template<typename CELL> static typename CELL::ALLOCATOR::pointer create(){return helper<CELL>::go();}
	pool(char* buffer,size_t buffer_size,size_t cell_size,size_t payload_offset,size_t type_id,f_ptr get_size_generic):buffer(buffer),buffer_size(buffer_size),cell_size(cell_size),payload_offset(payload_offset),type_id(type_id),writable(true),get_size_generic(get_size_generic){
		cerr<<"new pool "<<(void*)buffer<<endl;
	}
	struct test{
		const size_t type_id;
		test(size_t type_id):type_id(type_id){}
		bool operator()(POOL_CELL& c)const{
			//cerr<<hex<<"test:"<<type_id<<"=="<<((pool*)c.body.payload)->type_id<<endl;
			return ((pool*)c.body.payload)->type_id==type_id;
		}
	};

	template<typename CELL> CELL* get_cells(){
		return (CELL*)buffer;
	}
	//size in cells
	size_t size() const{return buffer_size/cell_size;}
	template<typename CELL> void status(){
		CELL *c=(CELL*)buffer;
		cerr<<"pool "<<c[0].body.info.size<<"/"<<buffer_size/sizeof(CELL)<<" cell(s) "<<endl;
	}
	//should only allocate 1 cell at a time, must not be mixed with allocate()!
	template<typename CELL> typename CELL::INDEX allocate_at(typename CELL::INDEX i,size_t n){
		CELL *c=(CELL*)buffer;
		typedef typename CELL::INDEX INDEX;
		//should check if cells available
		//CELL::check(c[i]);//bounds checking
		c[0].body.info.size+=n;//update total number of cells in use
		CELL::post_allocate(c+i,c+i+n);
		return i;
	}
	template<typename CELL> typename CELL::INDEX allocate(size_t n){
		CELL *c=(CELL*)buffer;
		typedef typename CELL::INDEX INDEX;
		INDEX prev=0,current=c[prev].body.info.next;
		//cerr<<"current: "<<(int)current<<endl;
		while(current && c[current].body.info.size<n){
			cerr<<"\t"<<(int)current<<endl;
			prev=current;
			current=c[prev].body.info.next;
		}
		if(current){ //we have found enough contiguous cells
			if(c[current].body.info.size==n){
				//cerr<<"found!"<<(int)prev<<"\t"<<(int)current<<"\t"<<(int)c[current].body.info.next<<endl;
				c[prev].body.info.next=c[current].body.info.next;
			}else{	//create new group
				INDEX i=current+n;
				c[prev].body.info.next=i;
				c[i].body.info.size=c[current].body.info.size-n;
				c[i].body.info.next=c[current].body.info.next;
			}
			//shall we clean up this cell???
			//we could although it is not necessary, an allocator does not have to initialize the memory
			c[current].body.info.size=0;
			c[current].body.info.next=0;
			
			c[0].body.info.size+=n;//update total number of cells in use
			CELL::post_allocate(c+current,c+current+n);
		}else{
			//let's see how many cells we need to fulfill demand
			cerr<<"new buffer size:"<<buffer_size+n*cell_size<<"\t"<<CELL::MAX_SIZE*cell_size<<endl;
			if(buffer_size+n*cell_size>CELL::MAX_SIZE*cell_size) throw std::bad_alloc();
			size_t new_buffer_size=min<size_t>(max<size_t>(buffer_size+n*cell_size,2*buffer_size),CELL::MAX_SIZE*cell_size);
			cerr<<this<<" increasing pool size from "<<buffer_size<<" to "<<new_buffer_size<<endl;
			//at this stage we may decide to increase the original buffer
			//we need to create new buffer, copy in the old one
			typename CELL::RAW_ALLOCATOR raw;
			auto new_buffer=raw.allocate(new_buffer_size);
			//the next 3 stages must be avoided when dealing with mmap
			if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value){
				memcpy(new_buffer,buffer,buffer_size);
				memset(new_buffer+buffer_size,0,new_buffer_size-buffer_size);
				raw.deallocate(buffer,buffer_size);	
			}
			buffer=new_buffer;
			CELL *c=(CELL*)buffer;
			//add the new range
			c[buffer_size/cell_size].body.info.size=(new_buffer_size-buffer_size)/cell_size;
			c[buffer_size/cell_size].body.info.next=c[0].body.info.next;
			c[0].body.info.next=buffer_size/cell_size;	
			buffer_size=new_buffer_size;
			return allocate<CELL>(n);
		}
		cerr<<this<<" allocate "<<n<<" cell(s) at index "<<(int)current<<endl;
		return current;
	}	
	template<typename CELL> void deallocate(typename CELL::INDEX index,size_t n){
		cerr<<this<<" deallocate "<<n<<" cell(s) at index "<<(int)index<<endl;
		CELL *c=(CELL*)buffer;
		typedef typename CELL::INDEX INDEX;
		c[index].body.info.size=n;
		c[index].body.info.next=c[0].body.info.next;
		c[0].body.info.next=index;
		c[0].body.info.size-=n;//update total number of cells in use
		CELL::post_deallocate(c+index,c+index+n);
	}
	template<typename CELL> typename CELL::PAYLOAD& get_payload(typename CELL::INDEX index){
		//cerr<<this<<" dereference cell at index "<<(int)index<<endl;
		CELL *c=(CELL*)buffer;
		CELL::check(c[index]);//bounds checking
		return (typename CELL::PAYLOAD&)c[index].body.payload;	
	}
	//if we iterate 
	template<typename CELL> CELL& get_cell_cast(typename CELL::INDEX index){
		return (CELL&)(*(buffer+index*cell_size));
	}
	//using CELL is a bit confusing
	template<typename CELL> typename CELL::PAYLOAD& get_cast(typename CELL::INDEX index){
		//we don't know the actual type, only cell_size and payload_offset
		return (typename CELL::PAYLOAD&)(*(buffer+index*cell_size+payload_offset));
	}
	template<typename CELL> static typename CELL::ALLOCATOR::pointer get_pool(){
		static auto p=create<CELL>();
		return p;
	}
	//iterator, need to add safeguards so that CELL makes sense!
	template<typename CELL> struct iterator{
		typedef size_t INDEX;
		typedef iterator pointer;
		typedef CELL value_type;
		typedef CELL& reference;
		typedef ptrdiff_t difference_type;
		typedef forward_iterator_tag iterator_category;
		pool& p;
		INDEX index;
		INDEX n;//number of cells to visit
		iterator(pool& p,INDEX index=0,INDEX n=0):p(p),index(index),n(n){
			while(!p.get_cell_cast<CELL>(index).management&&n) ++index;
		}
		iterator& operator++(){
			++index;
			while(!p.get_cell_cast<CELL>(index).management&&n) ++index;
			--n;
			return *this;
		}
		value_type* operator->()const{return &p.get_cell_cast<CELL>(index);}
		reference operator*()const{return p.get_cell_cast<CELL>(index);}
		bool operator==(const iterator& a)const{return n==a.n;}
		bool operator!=(const iterator& a)const{return n!=a.n;}
	};
	template<typename CELL> iterator<CELL> begin(){return iterator<CELL>(*this,1,get_size_generic(*this));}
	template<typename CELL> iterator<CELL> end(){return iterator<CELL>(*this,1,0);}
};
//operators
template<
	typename INDEX,
	typename PAYLOAD,
	typename ALLOCATOR,
	typename RAW_ALLOCATOR,
	typename MANAGEMENT
> pool::ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> operator+(const pool::ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& a,size_t s){
	pool::ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> tmp=a;
	return tmp+=s;
}
template<
	typename INDEX,
	typename PAYLOAD,
	typename ALLOCATOR,
	typename RAW_ALLOCATOR,
	typename MANAGEMENT
> pool::ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> operator-(const pool::ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& a,size_t s){
	pool::ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> tmp=a;
	return tmp-=s;
}
template<
	typename INDEX,
	typename PAYLOAD,
	typename ALLOCATOR,
	typename RAW_ALLOCATOR,
	typename MANAGEMENT
> ptrdiff_t operator-(const pool::ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& a,const pool::ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& b){
	return a.index-b.index;
}
template<
	typename _INDEX_,
	typename _PAYLOAD_,
	typename _ALLOCATOR_,
	typename _RAW_ALLOCATOR_,
	typename _MANAGEMENT_
> struct ptr_d{
	typedef _INDEX_ INDEX;
	typedef _PAYLOAD_ VALUE_TYPE;
	pool::POOL_ALLOCATOR::pointer pool_ptr;
	INDEX index;
	typedef ptr_d pointer;
	typedef VALUE_TYPE value_type;
	typedef VALUE_TYPE& reference;
	typedef ptrdiff_t difference_type;
	typedef random_access_iterator_tag iterator_category;
	//ptr_d(pool::POOL_ALLOCATOR::pointer pool_ptr=0,INDEX index=0):pool_ptr(pool_ptr),index(index){}
	ptr_d(pool::POOL_ALLOCATOR::pointer pool_ptr,INDEX index):pool_ptr(pool_ptr),index(index){}
	template<
		typename _OTHER_INDEX_,
		typename _OTHER_PAYLOAD_,
		typename _OTHER_ALLOCATOR_,
		typename _OTHER_RAW_ALLOCATOR_,
		typename _OTHER_MANAGEMENT_
	> ptr_d(const pool::ptr<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p):pool_ptr(pool::get_pool<cell<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>>()),index(p.index){
		VALUE_TYPE* a;
		_PAYLOAD_* b;
		a=b;
	}
	value_type* operator->()const{
		typedef cell<_INDEX_,_PAYLOAD_,_ALLOCATOR_,_RAW_ALLOCATOR_,_MANAGEMENT_> CELL;
		return &pool_ptr->get_cast<CELL>(index);
	}
	reference operator*()const{
		typedef cell<_INDEX_,_PAYLOAD_,_ALLOCATOR_,_RAW_ALLOCATOR_,_MANAGEMENT_> CELL;
		return pool_ptr->get_cast<CELL>(index);
	}
	ptr_d& operator+=(INDEX s){index+=s;return *this;}
	ptr_d& operator++(){++index;return *this;}
	ptr_d& operator-=(INDEX s){index-=s;return *this;}
	ptr_d& operator--(){--index;return *this;}
	bool operator==(const ptr_d& a)const{return pool_ptr==a.pool_ptr&&index==a.index;}
	bool operator!=(const ptr_d& a)const{return pool_ptr!=a.pool_ptr||index!=a.index;}
	operator value_type*() {return index ? operator->():0;}
	void _print(ostream& os)const{}
};
//convenience class, the allocator should be the only class exposed
template<
	typename _INDEX_,
	typename _PAYLOAD_,
	typename _ALLOCATOR_,
	typename _RAW_ALLOCATOR_=std::allocator<char>,
	typename _MANAGEMENT_=void
> struct pool_allocator:pool::allocator<_INDEX_,_PAYLOAD_,_ALLOCATOR_,_RAW_ALLOCATOR_,_MANAGEMENT_>{
	typedef ptr_d<_INDEX_,_PAYLOAD_,_ALLOCATOR_,_RAW_ALLOCATOR_,_MANAGEMENT_> derived_pointer;
	typedef derived_pointer const_derived_pointer;

};
//parameter order is different
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> struct persistent_allocator_managed:pool::allocator<INDEX,_PAYLOAD_,pool::POOL_ALLOCATOR,pool::mmap_allocator<_PAYLOAD_>,uint8_t>{
	typedef ptr_d<INDEX,_PAYLOAD_,pool::POOL_ALLOCATOR,pool::mmap_allocator<_PAYLOAD_>,uint8_t> derived_pointer;
	typedef derived_pointer const_derived_pointer;
	template<class OTHER_PAYLOAD> struct rebind{
		typedef persistent_allocator_managed<OTHER_PAYLOAD,INDEX> other;
	};
};
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> struct persistent_allocator_unmanaged:pool::allocator<INDEX,_PAYLOAD_,pool::POOL_ALLOCATOR,pool::mmap_allocator<_PAYLOAD_>,void>{
	typedef ptr_d<INDEX,_PAYLOAD_,pool::POOL_ALLOCATOR,pool::mmap_allocator<_PAYLOAD_>,void> derived_pointer;
	typedef derived_pointer const_derived_pointer;
	template<class OTHER_PAYLOAD> struct rebind{
		typedef persistent_allocator_unmanaged<OTHER_PAYLOAD,INDEX> other;
	};
};
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> struct volatile_allocator_managed:pool::allocator<INDEX,_PAYLOAD_,pool::POOL_ALLOCATOR,std::allocator<char>,uint8_t>{
	typedef ptr_d<INDEX,_PAYLOAD_,pool::POOL_ALLOCATOR,std::allocator<char>,uint8_t> derived_pointer;
	typedef derived_pointer const_derived_pointer;
	template<class OTHER_PAYLOAD> struct rebind{
		typedef volatile_allocator_managed<OTHER_PAYLOAD,INDEX> other;
	};
};
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> struct volatile_allocator_unmanaged:pool::allocator<INDEX,_PAYLOAD_,pool::POOL_ALLOCATOR,std::allocator<char>,void>{
	typedef ptr_d<INDEX,_PAYLOAD_,pool::POOL_ALLOCATOR,std::allocator<char>,void> derived_pointer;
	typedef derived_pointer const_derived_pointer;
	template<class OTHER_PAYLOAD> struct rebind{
		typedef volatile_allocator_unmanaged<OTHER_PAYLOAD,INDEX> other;
	};
};
template<
	typename _PAYLOAD_
> struct singleton_allocator{

};
	




	
#endif
