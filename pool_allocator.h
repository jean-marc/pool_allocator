#ifndef POOL_ALLOCATOR_H
#define POOL_ALLOCATOR_H
/*
 *	simplest allocator that works
 *
 */
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <typeinfo>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstddef>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <functional>
#include <algorithm>
#include <stdexcept>
#include <iomanip>
#include <cassert>
#include "ifthenelse.hpp"
using namespace std;
namespace pool_allocator{
	template<typename INDEX> struct _info{
		INDEX size;//size of current range in multiple of sizeof(info)
		INDEX next;//index of next available range 0 means no more range
	};
	template<> struct _info<void>{
	};
	/*
 	*	[...]:	cell
 	* 	s:		size
 	* 	n:		next	
 	* 	x:		payload
 	* 	
 	*	empty pool:
 	*	0			1			2			3			4			5			6			7			8			9				F
 	*	[s=0  | n=1][s=F  | n=0][          ][          ][          ][          ][          ][          ][          ][          ]... [          ]
 	*			 \__/	
 	* 	pool:
 	*	0			1			2			3			4			5			6			7			8			9				F
 	*	[s=A  | n=3][xxxxxxxxxx][xxxxxxxxxx][s=4  | n=8][          ][          ][          ][xxxxxxxxxx][s=1  | n=0][xxxxxxxxxx]...	[xxxxxxxxxx]
 	*			 \__________________________/	     \__________________________________________________/	
 	* 	full pool:
 	*	0			1			2			3			4			5			6			7			8			9				F
 	*	[s=F  | n=0][xxxxxxxxxx][xxxxxxxxxx][xxxxxxxxxx][xxxxxxxxxx][xxxxxxxxxx][xxxxxxxxxx][xxxxxxxxxx][xxxxxxxxxx][xxxxxxxxxx]...	[xxxxxxxxxx]
 	*
 	* 	first cell size member indicates the total number of used cell(s)
 	*/ 
	template<
		typename _PAYLOAD_, 	/* the actual payload */
		typename _INDEX_,	/* the type used as index */
		typename _ALLOCATOR_,	/* where the pool instance will be allocated, distinct from where the buffer is allocated */
		typename _RAW_ALLOCATOR_=std::allocator<char>,	/* where the buffer is allocated */
		//can we add MAX_SIZE so finer control: useful for ring buffer style allocation
		typename _MANAGEMENT_=void,	/* overhead to tag allocated cells (bool) and do reference counting (uint_8)*/
		typename _INFO_=_info<_INDEX_>
	> struct cell{
		typedef _PAYLOAD_ PAYLOAD;
		typedef _INDEX_ INDEX;
		typedef _ALLOCATOR_ ALLOCATOR;
		typedef _RAW_ALLOCATOR_ RAW_ALLOCATOR;
		typedef _MANAGEMENT_ MANAGEMENT;
		typedef _INFO_ INFO;
		//why would we make a copy?
		cell(const cell&)=delete;
		/*
 		*	would have management after payload give better packed structures?
 		*/ 
		MANAGEMENT management; //store metadata, not visible to payload, can also be used to detect unauthorized access
		union{
			INFO info;
			//could also use boost::aligned_storage
			//char payload[sizeof(PAYLOAD)];//will affect data alignment, we have to investigate the effect of misalignment (extra CPU + exception for special operations)
			PAYLOAD payload;
		}body;
		typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT,_info<void>> HELPER;	
		enum{MANAGED=true};
		enum{OPTIMIZATION=false};
		enum{FACTOR=1};
		//enum{MAX_SIZE=(1L<<(sizeof(INDEX)<<3))-1};//because cell[0] is used for management, wouldn't it be simpler if we stored that info somewhere else?
		enum{MAX_SIZE=std::numeric_limits<INDEX>::max()-1};
		static void post_allocate(cell* begin,cell* end){
			for(cell* i=begin;i<end;++i) i->management=0x1;//will increase for reference counting	
		}
		static void post_deallocate(cell* begin,cell* end){
			for(cell* i=begin;i<end;++i) i->management=0x0;	
		}
		static void is_available(cell* begin,cell* end){
			for(cell* i=begin;i<end;++i) 
				if(i->management) throw std::out_of_range("already allocated");	
		}
		//check if the cell has been allocated
		static void check(cell& c){
			if(!c.management) throw std::out_of_range(string("bad reference for ")+typeid(PAYLOAD).name());	
		}
		#ifdef REF_COUNT
		static void increase_ref_count(cell& c){++c.management;}
		static void decrease_ref_count(cell& c){--c.management;}
		static int get_ref_count(cell& c){return c.management;}
		#endif
	};
	template<
		typename _PAYLOAD_,
		typename _INDEX_,
		typename _ALLOCATOR_,
		typename _RAW_ALLOCATOR_,
		typename _INFO_
	> struct cell<_PAYLOAD_,_INDEX_,_ALLOCATOR_,_RAW_ALLOCATOR_,void,_INFO_>{
		typedef _PAYLOAD_ PAYLOAD;
		typedef _INDEX_ INDEX;
		typedef _ALLOCATOR_ ALLOCATOR;
		typedef void MANAGEMENT;
		typedef _INFO_ INFO;
		typedef _RAW_ALLOCATOR_ RAW_ALLOCATOR;
		cell(const cell&)=delete;
		union{
			INFO info;
			PAYLOAD payload;
		}body;
		typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,void,_info<void>> HELPER;	
		enum{MANAGED=false};
		//OPTIMIZATION can cause confusion, would be good to be able to turn it off
		enum{OPTIMIZATION=(sizeof(INFO)>sizeof(PAYLOAD))&&(sizeof(INFO)%sizeof(PAYLOAD)==0)};
		//enum{OPTIMIZATION=false};
		enum{FACTOR=OPTIMIZATION ? sizeof(INFO)/sizeof(PAYLOAD) : 1};
		//enum{MAX_SIZE=(1L<<(sizeof(INDEX)<<3))/FACTOR-1};
		enum{MAX_SIZE=std::numeric_limits<INDEX>::max()/FACTOR-1};
		static void post_allocate(cell*,cell*){}
		static void post_deallocate(cell*,cell*){}
		static void is_available(cell* begin,cell* end){}
		static void check(cell& c){}
		#ifdef REF_COUNT
		static void increase_ref_count(cell& c){}
		static void decrease_ref_count(cell& c){}
		static int get_ref_count(cell& c){return 0;}
		#endif
	};
	struct pool{
		//forward declaration of ptr_d for constructor
		template<
			typename _PAYLOAD_,
			typename _INDEX_,
			typename _ALLOCATOR_,
			typename _RAW_ALLOCATOR_,
			typename _MANAGEMENT_
		> struct ptr_d;
		template<
			typename VALUE_TYPE,//not consistent
			typename INDEX,
			typename ALLOCATOR,
			typename RAW_ALLOCATOR,
			typename MANAGEMENT
		> struct ptr{
			typedef cell<VALUE_TYPE,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
			INDEX index;
			typedef INDEX index_type;
			typedef ptr pointer;
			typedef VALUE_TYPE value_type;
			typedef value_type element_type;
			typedef VALUE_TYPE& reference;
			typedef ptrdiff_t difference_type;
			typedef random_access_iterator_tag iterator_category;
			/*explicit*/ ptr(INDEX index=0):index(index){}
			#ifdef REF_COUNT
			ptr(const ptr& p):index(p.index){
				if(is_same<MANAGEMENT,uint8_t>::value){	
					cerr<<"copy constructor"<<endl;	
					//cerr<<"management:"<<(int)pool::get_pool<CELL>()->get_cell_cast<CELL>(index).management<<endl;
					cerr<<"management:"<<CELL::get_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index))<<endl;
					CELL::increase_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index)); 
				}
			}
			ptr& operator=(const ptr& p){
				if(is_same<MANAGEMENT,uint8_t>::value){	
					cerr<<"copy operator"<<endl;	
					if(index){
						//if(pool::get_pool<CELL>()->get_cell_cast<CELL>(index).management==1){
						if(CELL::get_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index))==1){
							(*this)->~VALUE_TYPE();
							allocator<VALUE_TYPE,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> a;
							pool::get_pool<CELL>()->template deallocate<CELL>(index/CELL::FACTOR,max<size_t>(1/CELL::FACTOR,1));
							//a.deallocate(*this,1);//makes a copy
						}else{
							//--pool::get_pool<CELL>()->get_cell_cast<CELL>(index).management;
							CELL::decrease_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index)); 
						}
					}
					index=p.index;
					//++pool::get_pool<CELL>()->get_cell_cast<CELL>(index).management; 
					CELL::increase_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index)); 
				}else{
					index=p.index;
				}
				return *this;
			}
			~ptr(){
				if(is_same<MANAGEMENT,uint8_t>::value){	
					cerr<<"~ptr"<<endl;
					//cerr<<"management:"<<(int)pool::get_pool<CELL>()->get_cell_cast<CELL>(index).management<<endl;
					cerr<<"management:"<<CELL::get_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index))<<endl;
					if(index){
						//if(pool::get_pool<CELL>()->get_cell_cast<CELL>(index).management==1){
						if(CELL::get_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index))==1){
							(*this)->~VALUE_TYPE();
							allocator<VALUE_TYPE,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> a;
							pool::get_pool<CELL>()->template deallocate<CELL>(index/CELL::FACTOR,max<size_t>(1/CELL::FACTOR,1));
							//a.deallocate(*this,1);//makes a copy
						}else{
							//--pool::get_pool<CELL>()->get_cell_cast<CELL>(index).management;
							CELL::decrease_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index)); 
						}
					}
				}
			}
			#endif
			//no casting between different types	
			template<
				typename _OTHER_PAYLOAD_,
				typename _OTHER_INDEX_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr(const ptr<_OTHER_PAYLOAD_,_OTHER_INDEX_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p)=delete;
			//wrong most of the time 
			template<
				typename _OTHER_PAYLOAD_,
				typename _OTHER_INDEX_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr(const ptr_d<_OTHER_PAYLOAD_,_OTHER_INDEX_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p);

			//dangerous
			/*
			* problem here with basic_string: short string optimization ends up writing in the pool
			* instead of local storage, there is no easy way to work around that limitation short of
			* disabling the optimization in the string implementation.
			* what about using the same allocator for the string itself? not possible
			* work-around: reserve size to guarantee allocator is used
			*
			*/ 
			/*
			explicit ptr(const value_type* p){
				cerr<<"~~~"<<(void*)pool::get_pool<CELL>()->buffer<<"\t"<<(void*)p<<endl;			
				//auto pool_ptr=pool::get_pool<CELL>();
				//if((void*)p<(void*)pool_ptr->buffer||(void*)p>(void*)(pool_ptr->buffer+pool_ptr->buffer_size))
				//	throw std::runtime_error("pointer does not belong to pool");
				index=0;//we have to make sure it is never dereferenced
			}
			*/
			//static ptr pointer_to(element_type&){}
			value_type* operator->()const{
				typedef typename IfThenElse<CELL::OPTIMIZATION,typename CELL::HELPER,CELL>::ResultT PAYLOAD_CELL;
				return &pool::get_pool<CELL>()->template get_payload<PAYLOAD_CELL>(index);
			}
			reference operator*()const{
				if(!index) throw std::runtime_error("wrong reference"); 
				typedef typename IfThenElse<CELL::OPTIMIZATION,typename CELL::HELPER,CELL>::ResultT PAYLOAD_CELL;
				return pool::get_pool<CELL>()->template get_payload<PAYLOAD_CELL>(index);
			}
			ptr& operator+=(INDEX s){index+=s;return *this;}
			ptr& operator++(){++index;return *this;}
			ptr& operator-=(INDEX s){index-=s;return *this;}
			ptr& operator--(){--index;return *this;}
			bool operator==(const ptr& a)const{return index==a.index;}
			bool operator!=(const ptr& a)const{return index!=a.index;}
			bool operator<(const ptr& a)const{return index<a.index;}
			bool operator>(const ptr& a)const{return index>a.index;}
			/*
 			*  template <typename FancyPtr>
 			*  auto trueaddress(FancyPtr ptr) {  
 			*  	return !ptr ? nullptr : std::addressof(*ptr); 
 			*  }
 			*  see https://rawgit.com/google/cxx-std-draft/allocator-paper/allocator_user_guide.html
 			*/
			operator value_type*(){return index ? operator->():0;}
			//next lines causes g++ to complain but still build
			//operator value_type*() const{return index ? operator->():0;}
			//bool operator==(value_type* a)const{return operator->()==a;}
			void _print(ostream& os)const{}
		};
		//specialized for const VALUE_TYPE
		template<
			typename VALUE_TYPE,	
			typename INDEX,
			typename ALLOCATOR,
			typename RAW_ALLOCATOR,
			typename MANAGEMENT
		> struct ptr<const VALUE_TYPE,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>{
			typedef cell<VALUE_TYPE,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
			INDEX index;
			typedef ptr pointer;
			typedef const VALUE_TYPE value_type;
			typedef value_type element_type;
			typedef const VALUE_TYPE& reference;
			typedef ptrdiff_t difference_type;
			typedef random_access_iterator_tag iterator_category;
			ptr(INDEX index=0):index(index){}
			ptr(const ptr<VALUE_TYPE,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& p):index(p.index){}
			//no casting between different types	
			template<
				typename _OTHER_PAYLOAD_,
				typename _OTHER_INDEX_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr(const ptr<_OTHER_PAYLOAD_,_OTHER_INDEX_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p)=delete;
			//wrong most of the time 
			template<
				typename _OTHER_PAYLOAD_,
				typename _OTHER_INDEX_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr(const ptr_d<_OTHER_PAYLOAD_,_OTHER_INDEX_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p);
			value_type* operator->()const{
				typedef typename IfThenElse<CELL::OPTIMIZATION,typename CELL::HELPER,CELL>::ResultT PAYLOAD_CELL;
				//should use different call because it return a pointer to const, get_const_payload?
				return &pool::get_pool<CELL>()->template get_payload<PAYLOAD_CELL>(index);
			}
			reference operator*()const{
				typedef typename IfThenElse<CELL::OPTIMIZATION,typename CELL::HELPER,CELL>::ResultT PAYLOAD_CELL;
				return pool::get_pool<CELL>()->template get_payload<PAYLOAD_CELL>(index);
			}
			ptr& operator+=(INDEX s){index+=s;return *this;}
			ptr& operator++(){++index;return *this;}
			ptr& operator-=(INDEX s){index-=s;return *this;}
			ptr& operator--(){--index;return *this;}
			bool operator==(const ptr& a)const{return index==a.index;}
			bool operator!=(const ptr& a)const{return index!=a.index;}
			bool operator<(const ptr& a)const{return index<a.index;}
			bool operator>(const ptr& a)const{return index>a.index;}
			operator value_type*() const {return index ? operator->():0;}
			void _print(ostream& os)const{}
		};
		/*
		* 	iterators to visit pool in a generic way, we need to skip non-allocated ranges
		*	end iterator is tricky because we don't know where the last cell is, because they are
		*	allocated at random positions, we could keep track of last cell but it would take extra work
		*	at each allocation/deallocation
		*	it is very similar to ptr_d, maybe it could inherit from it, the problem is to access the MANAGEMENT member
		*	to test if the cell is being used
		*	shouldn't it be ptr instead of ptr_d?
		*/ 
		template<
			typename PAYLOAD,
			typename INDEX,
			typename ALLOCATOR,//not needed
			typename RAW_ALLOCATOR,//not needed
			typename MANAGEMENT
		>class cell_iterator{
			friend struct pool;
			INDEX index;
			INDEX cell_index;
		public:
			typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
			typedef cell_iterator pointer;
			typedef PAYLOAD value_type;
			typedef PAYLOAD& reference;
			typedef ptrdiff_t difference_type;
			typedef forward_iterator_tag iterator_category;
			cell_iterator(INDEX index=0):index(index),cell_index(1){}
			cell_iterator& operator++(){
				++index;
				++cell_index;//otherwise always stays on same cell
				return *this;
			}
			value_type* operator->(){//not const
				while(!pool::get_pool<CELL>()->template get_cells<CELL>()[cell_index].management) ++cell_index;
				return &pool::get_pool<CELL>()->template get_cells<CELL>()[cell_index].body.payload;
			}	
			reference operator*(){
				while(!pool::get_pool<CELL>()->template get_cells<CELL>()[cell_index].management) ++cell_index;
				return pool::get_pool<CELL>()->template get_cells<CELL>()[cell_index].body.payload;
			}
			bool operator==(const cell_iterator& a)const{return index==a.index;}
			bool operator<(const cell_iterator& a)const{return index<a.index;}
			bool operator!=(const cell_iterator& a)const{return index!=a.index;}
			INDEX get_cell_index(){
				while(!pool::get_pool<CELL>()->template get_cells<CELL>()[cell_index].management) ++cell_index;
				return cell_index;
			}
			operator ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>(){return ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>(get_cell_index());}
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
				cerr<<"mmap_allocator::allocate()"<<endl;
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
		template<typename T> static size_t get_hash(){
			std::hash<std::string> str_hash;
			auto tmp=str_hash(typeid(T).name());
			//cerr<<typeid(T).name()<<"\t"<<tmp<<endl;
			//return str_hash(typeid(T).name());
			return tmp;
		}
		//confusing T is not the type allocated, just a unique type used for identification
		template<typename T> struct mmap_allocator{
			template<typename OTHER_PAYLOAD> struct rebind{
				typedef mmap_allocator<OTHER_PAYLOAD> other;
			};
			typedef char* pointer;
			//typedef T value_type;
			bool writable;
			static mmap_allocator_impl* get_impl(){
				/* 
 				* use the hash of the typeid in case names are too long or not valid file names 
				* symbolic links could be used to provide meaningful names
				*/
				ostringstream os;
				os<<"db/"<<setfill('0')<<hex<<setw(16)<<get_hash<T>();
				//static mmap_allocator_impl* a=new mmap_allocator_impl(string("db/")+typeid(T).name());
				static mmap_allocator_impl* a=new mmap_allocator_impl(os.str());
				return a;
			}
			//we know that there will be only one range used at any given time
			pointer allocate(size_t n){
				writable=get_impl()->writable;//a bit kludgy
				return get_impl()->allocate(n);
			}
			void deallocate(pointer p,size_t n){

			}
		};
		typedef ptr<pool,uint8_t,std::allocator<pool>,mmap_allocator<pool>,char> POOL_PTR;//MUST be consistent with POOL_ALLOCATOR definition
		//typedef allocator<uint8_t,pool,std::allocator<pool>,mmap_allocator<pool>,char> POOL_ALLOCATOR;
		template<
			typename _PAYLOAD_,
			typename _INDEX_,
			typename _ALLOCATOR_,
			typename _RAW_ALLOCATOR_,
			typename _MANAGEMENT_
		> struct ptr_d{
			typedef _PAYLOAD_ VALUE_TYPE;
			typedef _INDEX_ INDEX;
			POOL_PTR pool_ptr;
			INDEX index;
			typedef ptr_d pointer;
			typedef VALUE_TYPE value_type;
			typedef VALUE_TYPE& reference;
			typedef ptrdiff_t difference_type;
			typedef random_access_iterator_tag iterator_category;
			ptr_d(POOL_PTR pool_ptr=0,INDEX index=0):pool_ptr(pool_ptr),index(index){}
			//should not be allowed if OPTIMIZATION used in ptr
			template<
				typename _OTHER_PAYLOAD_,
				typename _OTHER_INDEX_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr_d(const ptr<_OTHER_PAYLOAD_,_OTHER_INDEX_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p):pool_ptr(get_pool<typename ptr<_OTHER_PAYLOAD_,_OTHER_INDEX_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>::CELL>()),index(p.index){
				_OTHER_PAYLOAD_* b=nullptr;
				VALUE_TYPE* a=b;
				#ifdef REF_COUNT
				//how do we know if the pool_ptr is ref counted?
				//derived class is not necessarily ref-counted
				#endif
			}
			//construct from cell_iterator
			template<
				typename _OTHER_PAYLOAD_,
				typename _OTHER_INDEX_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr_d(const cell_iterator<_OTHER_PAYLOAD_,_OTHER_INDEX_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& c):pool_ptr(get_pool<cell<_OTHER_PAYLOAD_,_OTHER_INDEX_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>>()),index(c.cell_index){
			}
			template<
				typename _OTHER_PAYLOAD_,
				typename _OTHER_INDEX_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr_d(const ptr_d<_OTHER_PAYLOAD_,_OTHER_INDEX_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p):pool_ptr(p.pool_ptr),index(p.index){
				//there is not enough information to check the validity of that operation
				//but there should be a relation-ship between PAYLOADS...
			}

			template<
				typename _OTHER_PAYLOAD_,
				typename _OTHER_INDEX_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> explicit operator ptr<_OTHER_PAYLOAD_,_OTHER_INDEX_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>(){
				//VALUE_TYPE* a;
				//_OTHER_PAYLOAD_* b=nullptr;
				//a=b;
				return ptr<_OTHER_PAYLOAD_,_OTHER_INDEX_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>(index);
			}
			value_type* operator->()const{
				//is there any range checking?: no!!!!!, because there is no easy access to management
				typedef cell<_PAYLOAD_,_INDEX_,_ALLOCATOR_,_RAW_ALLOCATOR_,_MANAGEMENT_> CELL;
				return &pool_ptr->get_payload_cast<CELL>(index);
			}
			reference operator*()const{
				typedef cell<_PAYLOAD_,_INDEX_,_ALLOCATOR_,_RAW_ALLOCATOR_,_MANAGEMENT_> CELL;
				return pool_ptr->get_payload_cast<CELL>(index);
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
		/*
 		*	Alloc may define a member template rebind for rebinding. 
 		*	This may be omitted if Alloc is itself a template class A<T, Args...> where the value type is the first template argument, 
 		*	in which case AllocTraits::rebind_alloc<U> defaults to A<U, Args...>. 
 		*/ 
		template<
			typename PAYLOAD,
			typename INDEX,
			typename ALLOCATOR,
			typename RAW_ALLOCATOR,
			typename MANAGEMENT
		> struct allocator{
			//troubleshooting
			typedef RAW_ALLOCATOR _RAW_ALLOCATOR_;
			typedef ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> pointer;
			typedef	ptr<const PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> const_pointer; 
			typedef ptr_d<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> generic_pointer;
			typedef ptr_d<const PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> const_generic_pointer;
			//legacy
			typedef generic_pointer derived_pointer;
			typedef const_generic_pointer const_derived_pointer;
			typedef typename pointer::value_type value_type;
			typedef typename pointer::reference reference;
			//we have to decide if const pointers use different pool
			typedef typename const_pointer::reference const_reference;
			//that is not correct!!!!
			typedef std::size_t size_type;
			typedef std::ptrdiff_t difference_type;
			allocator(){}
			~allocator(){}
			//this is a upper boundary of the maximum size 
			size_type max_size() const throw(){
				typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				return CELL::MAX_SIZE;
			}
			pointer allocate(size_type n){
				cerr<<"allocate "<<n<<" elements"<<endl;
				typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				return pointer(pool::get_pool<CELL>()->template allocate<CELL>(max<size_t>(ceil(1.0*n/CELL::FACTOR),1))*CELL::FACTOR);
			}
			pointer allocate_at(INDEX i,size_type n){
				typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				return pointer(pool::get_pool<CELL>()->template allocate_at<CELL>(i,max<size_t>(ceil(1.0*n/CELL::FACTOR),1))*CELL::FACTOR);
			}
			pointer ring_allocate(){
				//behaves like normal allocate until we reach maximum size after that return address of cell
				//what happens if a cell is deallocated?
				typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				if(pool::get_pool<CELL>()->template get_cells<CELL>()[0].body.info.size<CELL::MAX_SIZE) return allocate(1);
				static INDEX current=0;
				current=(current==CELL::MAX_SIZE) ? 1 : current+1;
				return pointer(current);
			}
			//what if derived_pointer? should cast but maybe not
			void deallocate(pointer p,size_type n){
				typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				pool::get_pool<CELL>()->template deallocate<CELL>(p.index/CELL::FACTOR,max<size_t>(ceil(1.0*n/CELL::FACTOR),1));
			}
			//if this function is needed it means the container does not use the pointer type and persistence will fail
			/*void deallocate(value_type* p,size_type n){

			}*/
			//we have to rebind the RAW_ALLOCATOR as well!!!!! except if std::allocator
			template<class OTHER_PAYLOAD> struct rebind{
				typedef typename IfThenElse<
					std::is_same<RAW_ALLOCATOR,std::allocator<char>>::value,
					RAW_ALLOCATOR, 
					typename RAW_ALLOCATOR::template rebind<OTHER_PAYLOAD>::other
				>::ResultT OTHER_RAW_ALLOCATOR;
				typedef allocator<OTHER_PAYLOAD,INDEX,ALLOCATOR,OTHER_RAW_ALLOCATOR,MANAGEMENT> other;
			};
			static typename ALLOCATOR::pointer get_pool(){
				typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
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
			/*
			template<typename... Args> void construct(value_type* p,Args... args){
				cerr<<"construct at "<<(void*)p<<endl;
				new(p) value_type(args...);
			}
			void destroy(value_type* p){
				cerr<<"destroy at "<<(void*)p<<endl;
				p->~value_type();
			}
			*/
			bool operator==(const allocator&)const{return true;}
			bool operator!=(const allocator&)const{return false;}
			size_t size() const{
				typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				return pool::get_pool<CELL>()->template get_cells<CELL>()[0].body.info.size;
			}
			//typed iterator
			typedef cell_iterator<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> iterator;
			typedef iterator const_iterator;
			iterator begin(){return iterator();}
			iterator end(){return iterator(size());}
			const_iterator cbegin(){return const_iterator();}
			const_iterator cend(){return const_iterator(size());}
			/*
			iterator begin(){return iterator(1);}
			iterator end(){return iterator(1,0);}
			const_iterator cbegin(){return const_iterator(1);}
			const_iterator cend(){return const_iterator(1,0);}
			*/
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
		//specialize for void
		template<
			typename INDEX,
			typename ALLOCATOR,
			typename RAW_ALLOCATOR,
			typename MANAGEMENT
		> struct allocator<void,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>{
			template<typename OTHER_PAYLOAD> struct rebind{
				typedef typename IfThenElse<
					std::is_same<RAW_ALLOCATOR,std::allocator<char>>::value,
					RAW_ALLOCATOR, 
					typename RAW_ALLOCATOR::template rebind<OTHER_PAYLOAD>::other
				>::ResultT OTHER_RAW_ALLOCATOR;
				typedef allocator<OTHER_PAYLOAD,INDEX,ALLOCATOR,OTHER_RAW_ALLOCATOR,MANAGEMENT> other;
			};
		};
		//
		//let's store pools in a pool...maximum 255 pools for now
		//typedef cell<uint8_t,pool,std::allocator<pool>,std::allocator<char>,char> POOL_CELL;
		typedef cell<pool,uint8_t,std::allocator<pool>,mmap_allocator<pool>,char> POOL_CELL;
		//typedef allocator<POOL_CELL> POOL_ALLOCATOR;
		typedef allocator<pool,uint8_t,std::allocator<pool>,mmap_allocator<pool>,char> POOL_ALLOCATOR;
		char* buffer;
		size_t buffer_size;//in byte, this causes problem when pool's buffer is not persisted 
		const size_t cell_size;//in byte
		const size_t stride;
		const size_t payload_offset;
		const size_t type_id;//
		const bool iterable;
		#ifdef REF_COUNT
		//we need to know if the pool's payload uses ref counting
		#endif
		bool writable;
		/*
		*	shall we store the number of cells in pool instead of the buffer (c[0].body.info.size)
		*	the only problem is if we save the pool but not the buffer, this could happen?
		*	that would make it available to generic iterators, alternatively we could have a function pointer
		*/ 
		typedef size_t (*f_ptr)(pool&);
		f_ptr get_size_generic;
		template<typename CELL> static size_t get_size(pool& p){return p.get_cells<CELL>()[0].body.info.size;}

		template<
			typename CELL,
			typename PAYLOAD=typename CELL::PAYLOAD
		> struct helper{
			static typename CELL::ALLOCATOR::pointer go(){
				/*
				*	maybe the pool has been persisted 
				*/ 
				std::hash<std::string> str_hash;
				//size_t type_id=str_hash(typeid(typename CELL::PAYLOAD).name());
				/*
 				* what if we use different allocators with same payload? it is better to hash the whole
 				* cell rather than only the payload's typeid
 				* caveat: risk of stale pools to be investigated
 				*/
				size_t type_id=str_hash(typeid(CELL).name());
				size_t cell_size=sizeof(CELL);
				size_t stride=CELL::OPTIMIZATION ? sizeof(typename CELL::PAYLOAD) : cell_size;
				size_t buffer_size=64*cell_size;
				typename CELL::ALLOCATOR a;
				//cerr<<"looking for pool `"<<typeid(typename CELL::PAYLOAD).name()<<"'"<<endl;
				cerr<<"looking for pool `"<<typeid(CELL).name()<<"'\t"<<hex<<type_id<<dec<<"\t"<<a.size()<<endl;
				auto i=std::find_if(a.cbegin(),a.cend(),[=](const pool& p){return p.type_id==type_id;});
				if(i==a.cend()){
					cerr<<"pool not found"<<endl;
					typename CELL::RAW_ALLOCATOR raw;//what is the payload?
					//cerr<<"RAW_ALLOCATOR:"<<typeid(typename CELL::RAW_ALLOCATOR::value_type).name()<<endl;
					//we could simplify a lot by giving filename to allocator
					auto buffer=raw.allocate(buffer_size);//should specialize so we can 
					if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value){
						cerr<<"resetting volatile memory"<<endl;
						memset(buffer,0,buffer_size);
					}
					CELL *c=(CELL*)buffer;
					if(c[0].body.info.size==0&&c[0].body.info.next==0){
						cerr<<"resetting the buffer"<<endl;
						c[0].body.info.size=0;//new pool
						c[0].body.info.next=1;
						c[1].body.info.size=buffer_size/sizeof(CELL)-1;
						c[1].body.info.next=0;
					}
					/*
 					*	warn if allocator uses local copy	
 					*	it is more serious than that: if the main pool is read-only all the other pools must be made read-only as well!
 					*/
					if(!pool::template get_pool<POOL_CELL>()->writable) cerr<<"Warning: change only made to local copy!"<<endl;
					auto p=a.allocate(1);
					if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value)
						cerr<<"create new pool at index "<<(size_t)p.index<<endl;
					else
						cerr<<"create new persistent pool at index "<<(size_t)p.index<<endl;
					f_ptr f=pool::get_size<CELL>;
					a.construct(p,buffer,buffer_size,cell_size,stride,offsetof(CELL,body),type_id,true,CELL::MANAGED,f/*pool::get_size<CELL>*/);
					return p;
				}else{
					cerr<<"pool found at index "<<(size_t)i.cell_index<<endl;
					typename CELL::RAW_ALLOCATOR raw;
					auto buffer=raw.allocate(buffer_size);//at this stage we know if it is writable or not
					if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value){
						cerr<<"resetting volatile memory"<<endl;
						memset(buffer,0,buffer_size);
					}
					CELL *c=(CELL*)buffer;
					if(c[0].body.info.size==0&&c[0].body.info.next==0){//also used if file has been deleted
						cerr<<"resetting the buffer"<<endl;
						c[0].body.info.size=0;//new pool
						c[0].body.info.next=1;
						c[1].body.info.size=buffer_size/sizeof(CELL)-1;
						c[1].body.info.next=0;
					}
					//we need a pointer to the pool	
					typename CELL::ALLOCATOR::pointer p(i);
					//sanity check: has anything changed?
					cerr<<p->cell_size<<" vs "<<cell_size<<endl;
					cerr<<p->payload_offset<<" vs "<<offsetof(CELL,body)<<endl;
					cerr<<p->iterable<<" vs "<<CELL::MANAGED<<endl;
					if(p->cell_size==cell_size&&p->stride==stride&&p->payload_offset==offsetof(CELL,body)&&p->iterable==CELL::MANAGED){
						/*
 						*	this is a problem if multiple processes use the same db: the last
 						*	process started will cause segfault in running process, is there anywhere
 						*	else we can store that data?
 						*	It would also be nice to be able to make the database read-only for testing purpose
 						*/
						//we only have to refresh the buffer and function pointers
						cerr<<"modifying pool struct..."<<endl;
						p->buffer=buffer;//this will segfault if mounted read-only, what can we do????
						p->get_size_generic=pool::get_size<CELL>;
						// we also have to reset buffer_size if not persisted
						if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value)
							p->buffer_size=buffer_size;
						return p;//could we return a different pointer? dangerous: the index is used to allocate rdfs::Class
					}else{
						throw runtime_error("persisted class has been modified");
					}
				}
			}
		};
		template<
			typename CELL
		> struct helper<CELL,pool>{
			static typename CELL::ALLOCATOR::pointer go(){
				std::hash<std::string> str_hash;
				size_t type_id=str_hash(typeid(pool).name());//use the payload instead of cell type for consistency with filename
				size_t cell_size=sizeof(CELL);
				size_t stride=CELL::OPTIMIZATION ? sizeof(typename CELL::PAYLOAD) : cell_size;
				size_t buffer_size=64*cell_size;//this is dangerous because the file_size might be bigger!
				typename CELL::RAW_ALLOCATOR raw;
				auto buffer=raw.allocate(buffer_size);//what about allocating CELL's instead of char?, it would have the advantage of aligning the data
				if(!raw.writable){
					cerr<<"copying memory-mapped file to RAM"<<endl;
					auto tmp=new char[buffer_size];
					memcpy(tmp,buffer,buffer_size);
					buffer=tmp;//we wont use the mapping	
					/*
 					*	there should be warning messages when any cell in this pool will be modified
 					*/ 
				}	
				CELL *c=(CELL*)buffer;
				if(c[0].body.info.size==0&&c[0].body.info.next==0){
					c[0].body.info.size=0;//new pool
					c[0].body.info.next=1;
					c[1].body.info.size=buffer_size/sizeof(CELL)-1;
					c[1].body.info.next=0;
				}
				typename CELL::ALLOCATOR a;
				auto p=a.allocate(1);
				f_ptr f=pool::get_size<CELL>;
				a.construct(p,buffer,buffer_size,cell_size,stride,offsetof(CELL,body),type_id,raw.writable,CELL::MANAGED,f/*pool::get_size<CELL>*/);
				return p;
			}
		};

		template<typename CELL> static typename CELL::ALLOCATOR::pointer create(){return helper<CELL>::go();}

		pool(char* buffer,size_t buffer_size,size_t cell_size,size_t stride,size_t payload_offset,size_t type_id,bool writable,bool iterable,f_ptr get_size_generic):buffer(buffer),buffer_size(buffer_size),cell_size(cell_size),stride(stride),payload_offset(payload_offset),type_id(type_id),writable(writable),iterable(iterable),get_size_generic(get_size_generic){
			cerr<<"new pool "<<(void*)buffer<<endl;
			cerr<<"\tbuffer size:"<<buffer_size<<"\n";
			cerr<<"\tcell size:"<<cell_size<<"\n";
			cerr<<"\tstride:"<<stride<<"\n";
			cerr<<"\tpayload offset:"<<payload_offset<<"\n";
			cerr<<"\ttype id:"<<hex<<type_id<<dec<<"\n";
			cerr<<"\twritable:"<<writable<<"\n";
			cerr<<"\titerable:"<<iterable<<"\n";
		}
		~pool(){
			cerr<<"~pool()"<<endl;
		}
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
			//check if cells available
			CELL::is_available(c+i,c+i+n);
			c[0].body.info.size+=n;//update total number of cells in use
			CELL::post_allocate(c+i,c+i+n);
			return i;
		}
		template<typename CELL> typename CELL::INDEX allocate(size_t n){
			/*
 			*	how can we make this robust in case of crash?
 			*	what about simple checksum to see if pool healthy?, what about 
 			*	setting a bit before starting and resetting at the end?, 
 			*	could do the same with constructor
 			*	can we keep a journal with the changes made to the structure?
 			*
 			*/ 
			#ifdef POOL_ALLOCATOR_VERBOSE
			display<CELL>();
			#endif
			CELL *c=(CELL*)buffer;
			typedef typename CELL::INDEX INDEX;
			INDEX prev=0,current=c[prev].body.info.next;
			//cerr<<"current: "<<(int)current<<endl;
			//this should be made thread-safe
			while(current && c[current].body.info.size<n){
				cerr<<"\t"<<(int)current<<endl;
				prev=current;
				current=c[prev].body.info.next;
			}
			if(current){ //we have found enough contiguous cells
				if(c[current].body.info.size==n){
					//cerr<<"found!"<<(int)prev<<"\t"<<(int)current<<"\t"<<(int)c[current].body.info.next<<endl;
					/* 1 WRITE */
					c[prev].body.info.next=c[current].body.info.next;
				}else{	//create new group
					INDEX i=current+n;
					/* 3 WRITES */
					c[prev].body.info.next=i;
					c[i].body.info.size=c[current].body.info.size-n;
					c[i].body.info.next=c[current].body.info.next;
				}
				//shall we clean up this cell???
				//we could although it is not necessary, an allocator does not have to initialize the memory
				/* 2 WRITES */
				c[current].body.info.size=0;
				c[current].body.info.next=0;
				/*
				* update total number of cells in use: if this write fails it will leave pool in inconsistent state
				*/
				/* 1 WRITE */	
				c[0].body.info.size+=n;
				CELL::post_allocate(c+current,c+current+n);
			}else{
				//let's see how many cells we need to fulfill demand
				//this is wrong because maybe the buffer is not used but hard to tell if not ordered
				//it all depends where the last cell is
				size_t new_size=0;
				//does not work if prev is 0!!!
				if(prev>0 && prev+c[prev].body.info.size==buffer_size/cell_size){
					cerr<<"last cell!"<<endl;
					new_size=n-c[prev].body.info.size;
				}else{
					new_size=n;
				}
				cerr<<"new size:"<<(buffer_size/cell_size)+new_size<<"\tmax:"<<(CELL::MAX_SIZE+1)<<endl;
				if(((buffer_size/cell_size)+new_size-1)>(CELL::MAX_SIZE)) throw std::bad_alloc();
				size_t new_buffer_size=buffer_size+new_size*cell_size;
				/*
				#ifdef OPTIM_POS
				//grow last region	
				cerr<<"upper boundary:"<<prev+n<<endl;
				if((prev+n)>CELL::MAX_SIZE) throw std::bad_alloc();
				#else
				cerr<<"new buffer size:"<<buffer_size+n*cell_size<<"\t"<<CELL::MAX_SIZE*cell_size<<endl;
				if((buffer_size+n*cell_size)>(CELL::MAX_SIZE*cell_size)) throw std::bad_alloc();
				#endif
				size_t new_buffer_size=min<size_t>(max<size_t>(buffer_size+n*cell_size,2*buffer_size),CELL::MAX_SIZE*cell_size);
				*/
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
				#ifdef OPTIM_POS
				/*
 				*	add after last region
 				*/ 
				current=buffer_size/cell_size;	
				c[current].body.info.size=(new_buffer_size-buffer_size)/cell_size;
				c[current].body.info.next=0;
				c[prev].body.info.next=current;	
				//connect
				if(prev>0 && prev+c[prev].body.info.size==current){
					c[prev].body.info.size+=c[current].body.info.size;
					c[prev].body.info.next=c[current].body.info.next;
				}
				#else
				c[buffer_size/cell_size].body.info.size=(new_buffer_size-buffer_size)/cell_size;
				c[buffer_size/cell_size].body.info.next=c[0].body.info.next;
				c[0].body.info.next=buffer_size/cell_size;	
				#endif
				buffer_size=new_buffer_size;
				return allocate<CELL>(n);
			}
			cerr<<this<<" allocate "<<n<<" cell(s) at index "<<(int)current<<" for "<<hex<<typeid(CELL).name()<<dec<<endl;
			return current;
		}	
		template<typename CELL> void display() const{
			CELL *c=(CELL*)buffer;
			typedef typename CELL::INDEX INDEX;
			INDEX prev=0,current=c[prev].body.info.next;
			while(current){
				cerr<<(int)current<<"\t"<<(int)c[current].body.info.size<<"\t"<<(int)c[current].body.info.next<<endl;
				prev=current;
				current=c[prev].body.info.next;
			}
		}
		template<typename CELL> void deallocate(typename CELL::INDEX index,size_t n){
			cerr<<this<<" deallocate "<<n<<" cell(s) at index "<<(int)index<<endl;
			CELL *c=(CELL*)buffer;
			typedef typename CELL::INDEX INDEX;
			#ifdef POOL_ALLOCATOR_VERBOSE
			display<CELL>();
			#endif
			#ifdef OPTIM_POS
			/*
 			*	we should insert at right position and connect adjacent regions
 			*/ 
			INDEX prev=0,current=c[prev].body.info.next;
			while(current && current<index){
				cerr<<"\t"<<(int)current<<endl;
				prev=current;
				current=c[prev].body.info.next;
			}
			/*
 			* 4 possibilities:
 			* 	.connected to prev
 			* 	.isolated
 			* 	.connected to current
 			* 	.connected to both (bingo!)
 			*/
			if(current){
				if(prev+c[prev].body.info.size==index){//connected to prev
					if(index+n==current){//perfect fit
						c[prev].body.info.size+=n+c[current].body.info.size;
						c[prev].body.info.next=c[current].body.info.next;
					}else{
						c[prev].body.info.size+=n;
						c[prev].body.info.next=current;
					}
				}else{
					if(index+n==current){//connected to current
						c[index].body.info.size=n+c[current].body.info.size;
						c[index].body.info.next=c[current].body.info.next;
						c[prev].body.info.next=index;
					}else{//isolated
						c[index].body.info.size=n;
						c[index].body.info.next=current;
						c[prev].body.info.next=index;
					}
				}	
				c[0].body.info.size-=n;//update total number of cells in use
			}else{//at end of array
				if(prev+c[prev].body.info.size==index){//connected to prev
					c[prev].body.info.size+=n;
					c[prev].body.info.next=current;
				}else{
					c[index].body.info.size=n;
					c[index].body.info.next=current;
					c[prev].body.info.next=index;
				}
			}
			#else
			c[index].body.info.size=n;
			c[index].body.info.next=c[0].body.info.next;
			c[0].body.info.next=index;//the last de-allocated region is always first: not optimal 
			c[0].body.info.size-=n;//update total number of cells in use
			#endif
			CELL::post_deallocate(c+index,c+index+n);
		}
		template<typename CELL> typename CELL::PAYLOAD& get_payload(typename CELL::INDEX index){
			//can we make a thread-safe version of this? actually what if we make a copy?
			//cerr<<this<<" dereference cell at index "<<(int)index<<endl;
			CELL *c=(CELL*)buffer;
			//what if buffer gets modified here because of pool increase?
			CELL::check(c[index]);//bounds checking
			//return (typename CELL::PAYLOAD&)c[index].body.payload;	
			return c[index].body.payload;	
		}
		//if we iterate 
		//SHOULD ONLY BE USED TO ACCESS management
		template<typename CELL> CELL& get_cell_cast(typename CELL::INDEX index){
			//this is not good it should take payload_offset into account
			return (CELL&)(*(buffer+index*cell_size));
		}
		template<typename CELL> typename CELL::PAYLOAD& get_payload_cast(typename CELL::INDEX index){
			//we don't know the actual type, only cell_size and payload_offset
			char* p=buffer+index*stride;//we assume that a payload offset means there is a management
			//cerr<<"management?:"<<payload_offset<<"\t"<<(int)(uint8_t)(*p)<<endl;
			//if(payload_offset&&!(bool)(*p)) throw std::out_of_range("bad reference");	
			return (typename CELL::PAYLOAD&)(*(p+payload_offset));
		}
		/*
		*	find the pool or create it if it does not exist
		*/ 
		template<typename CELL> static typename CELL::ALLOCATOR::pointer get_pool(){
			/*
 			* should be thread safe according to:
			* http://stackoverflow.com/questions/8102125/is-local-static-variable-initialization-thread-safe-in-c11
			*/
			static auto p=create<CELL>();
			return p;
		}
		//iterator, need to add safeguards so that CELL makes sense!
		//what we need is INDEX and PAYLOAD and MANAGEMENT,
		template<typename CELL> struct iterator{
			typedef size_t INDEX;
			typedef iterator pointer;
			typedef typename CELL::PAYLOAD value_type;
			typedef typename CELL::PAYLOAD& reference;
			typedef ptrdiff_t difference_type;
			typedef forward_iterator_tag iterator_category;
			POOL_PTR pool_ptr;
			INDEX cell_index;
			INDEX index;
			iterator(POOL_PTR pool_ptr,INDEX index=0):pool_ptr(pool_ptr),index(index),cell_index(1){
				if(!pool_ptr->iterable) throw std::runtime_error("pool not iterable");
			}
			iterator& operator++(){
				++index;
				++cell_index;//otherwise always stays on same cell
				return *this;
			}
			value_type* operator->(){
				while(!pool_ptr->get_cell_cast<CELL>(cell_index).management) ++cell_index;
				return &pool_ptr->get_payload_cast<CELL>(cell_index);
			}
			reference operator*(){
				while(!pool_ptr->get_cell_cast<CELL>(cell_index).management) ++cell_index;
				return pool_ptr->get_payload_cast<CELL>(cell_index);
			}
			bool operator==(const iterator& a)const{return index==a.index;}
			bool operator!=(const iterator& a)const{return index!=a.index;}
			bool operator<(const iterator& a)const{return index<a.index;}
			INDEX get_cell_index(){
				while(!pool_ptr->get_cell_cast<CELL>(cell_index).management) ++cell_index;
				return cell_index;
			}
			template<
				typename PAYLOAD,
				typename INDEX,
				typename ALLOCATOR,
				typename RAW_ALLOCATOR,
				typename MANAGEMENT
			> operator ptr_d<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>(){return ptr_d<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>(pool_ptr,get_cell_index());} 
		};
		//we need a pool_ptr to itself
		template<typename CELL> static iterator<CELL> begin(POOL_PTR pool_ptr){return iterator<CELL>(pool_ptr);}
		template<typename CELL> static iterator<CELL> end(POOL_PTR pool_ptr){return iterator<CELL>(pool_ptr,pool_ptr->get_size_generic(*pool_ptr));}
		template<typename CELL> static iterator<CELL> cbegin(POOL_PTR pool_ptr){return iterator<CELL>(pool_ptr);}
		template<typename CELL> static iterator<CELL> cend(POOL_PTR pool_ptr){return iterator<CELL>(pool_ptr,pool_ptr->get_size_generic(*pool_ptr));}
	};
	//operators
	template<
		typename PAYLOAD,
		typename INDEX,
		typename ALLOCATOR,
		typename RAW_ALLOCATOR,
		typename MANAGEMENT
	> pool::ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> operator+(const pool::ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& a,size_t s){
		pool::ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> tmp=a;
		return tmp+=s;
	}
	template<
		typename PAYLOAD,
		typename INDEX,
		typename ALLOCATOR,
		typename RAW_ALLOCATOR,
		typename MANAGEMENT
	> pool::ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> operator-(const pool::ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& a,size_t s){
		pool::ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> tmp=a;
		return tmp-=s;
	}
	template<
		typename PAYLOAD_A,
		typename PAYLOAD_B,
		typename INDEX,
		typename ALLOCATOR,
		typename RAW_ALLOCATOR,
		typename MANAGEMENT
	> ptrdiff_t operator-(const pool::ptr<PAYLOAD_A,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& a,const pool::ptr<PAYLOAD_B,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& b){
		return a.index-b.index;
	}
	//implementation of ptr(const ptr_d&)
	template<typename PAYLOAD,typename INDEX,typename ALLOCATOR,typename RAW_ALLOCATOR,typename MANAGEMENT>
	template<typename OTHER_PAYLOAD,typename OTHER_INDEX,typename OTHER_ALLOCATOR,typename OTHER_RAW_ALLOCATOR,typename OTHER_MANAGEMENT> 
	pool::ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>::ptr(
		const pool::ptr_d<OTHER_PAYLOAD,OTHER_INDEX,OTHER_ALLOCATOR,OTHER_RAW_ALLOCATOR,OTHER_MANAGEMENT>& p
	){
		//not inside assert because compiler complaints
		auto pp=get_pool<typename pool::ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>::CELL>();
		//assert(pp==p.pool_ptr);//what if null pointer?
		assert(pp==p.pool_ptr||p.index==0);
		index=p.index;
	}
	//implementation of ptr(const ptr_d&)
	template<typename PAYLOAD,typename INDEX,typename ALLOCATOR,typename RAW_ALLOCATOR,typename MANAGEMENT>
	template<typename OTHER_PAYLOAD,typename OTHER_INDEX,typename OTHER_ALLOCATOR,typename OTHER_RAW_ALLOCATOR,typename OTHER_MANAGEMENT> 
	pool::ptr<const PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>::ptr(
		const pool::ptr_d<OTHER_PAYLOAD,OTHER_INDEX,OTHER_ALLOCATOR,OTHER_RAW_ALLOCATOR,OTHER_MANAGEMENT>& p
	){
		//not inside assert because compiler complaints
		auto pp=get_pool<typename pool::ptr<const PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>::CELL>();
		//assert(pp==p.pool_ptr);//what if null pointer?
		assert(pp==p.pool_ptr||p.index==0);
		index=p.index;
	}

}
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> using persistent_allocator_managed=pool_allocator::pool::allocator<
	_PAYLOAD_,
	INDEX,
	pool_allocator::pool::POOL_ALLOCATOR,
	pool_allocator::pool::template mmap_allocator<_PAYLOAD_>,
	bool
>;
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> using persistent_allocator_unmanaged=pool_allocator::pool::allocator<
	_PAYLOAD_,
	INDEX,
	pool_allocator::pool::POOL_ALLOCATOR,
	pool_allocator::pool::template mmap_allocator<_PAYLOAD_>,
	void
>;
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> using volatile_allocator_managed=pool_allocator::pool::allocator<
	_PAYLOAD_,
	INDEX,
	pool_allocator::pool::POOL_ALLOCATOR,
	std::allocator<char>,
	bool
>;
#ifdef REF_COUNT
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> using volatile_allocator_managed_rc=pool_allocator::pool::allocator<
	_PAYLOAD_,
	INDEX,
	pool_allocator::pool::POOL_ALLOCATOR,
	std::allocator<char>,
	uint8_t
>;

#endif
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> using volatile_allocator_unmanaged=pool_allocator::pool::allocator<
	_PAYLOAD_,
	INDEX,
	pool_allocator::pool::POOL_ALLOCATOR,
	std::allocator<char>,
	void
>;
template<
	typename _PAYLOAD_
> struct singleton_allocator{

};
#endif
