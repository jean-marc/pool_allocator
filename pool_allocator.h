#ifndef SPECIAL_ALLOCATOR_H
#define SPECIAL_ALLOCATOR_H
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

	template<
		typename _INDEX_,	/* the type used as index */
		typename _PAYLOAD_, 	/* the actual payload */
		typename _ALLOCATOR_,	/* where the pool instance will be allocated, distinct from where the buffer is allocated */
		typename _RAW_ALLOCATOR_=std::allocator<char>,	/* where the buffer is allocated */
		typename _MANAGEMENT_=void,	/* overhead to tag allocated cells (bool) and do reference counting (uint_8)*/
		typename _INFO_=_info<_INDEX_>
	> struct cell{
		typedef _INDEX_ INDEX;
		typedef _PAYLOAD_ PAYLOAD;
		typedef _ALLOCATOR_ ALLOCATOR;
		typedef _RAW_ALLOCATOR_ RAW_ALLOCATOR;
		typedef _MANAGEMENT_ MANAGEMENT;
		typedef _INFO_ INFO;
		//why would we make a copy?
		cell(const cell&)=delete;
		MANAGEMENT management; //store metadata, not visible to payload, can also be used to detect unauthorized access
		union{
			INFO info;
			//could also use boost::aligned_storage
			//char payload[sizeof(PAYLOAD)];//will affect data alignment, we have to investigate the effect of misalignment (extra CPU + exception for special operations)
			PAYLOAD payload;
		}body;
		typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT,_info<void>> HELPER;	
		enum{MANAGED=true};
		enum{OPTIMIZATION=false};
		enum{FACTOR=1};
		enum{MAX_SIZE=(1L<<(sizeof(INDEX)<<3))-1};//-1 because cell[0] is used for management
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
			if(!c.management) throw std::out_of_range("bad reference");	
		}
		#ifdef REF_COUNT
		static void increase_ref_count(cell& c){++c.management;}
		static void decrease_ref_count(cell& c){--c.management;}
		static int get_ref_count(cell& c){return c.management;}
		#endif
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
		cell(const cell&)=delete;
		union{
			INFO info;
			//char payload[sizeof(PAYLOAD)];//problem with data alignment, the members of PAYLOAD are no longer accessible
			PAYLOAD payload;
		}body;
		typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,void,_info<void>> HELPER;	
		enum{MANAGED=false};
		//OPTIMIZATION can cause confusion, would be good to be able to turn it off
		enum{OPTIMIZATION=(sizeof(INFO)>sizeof(PAYLOAD))&&(sizeof(INFO)%sizeof(PAYLOAD)==0)};
		//enum{OPTIMIZATION=false};
		enum{FACTOR=OPTIMIZATION ? sizeof(INFO)/sizeof(PAYLOAD) : 1};
		enum{MAX_SIZE=(1L<<(sizeof(INDEX)<<3))/FACTOR-1};
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
			typename _INDEX_,
			typename _PAYLOAD_,
			typename _ALLOCATOR_,
			typename _RAW_ALLOCATOR_,
			typename _MANAGEMENT_
		> struct ptr_d;
		template<
			typename INDEX,
			typename VALUE_TYPE,	
			typename ALLOCATOR,
			typename RAW_ALLOCATOR,
			typename MANAGEMENT
		> struct ptr{
			typedef cell<INDEX,VALUE_TYPE,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
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
							allocator<INDEX,VALUE_TYPE,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> a;
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
							allocator<INDEX,VALUE_TYPE,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> a;
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
				typename _OTHER_INDEX_,
				typename _OTHER_PAYLOAD_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr(const ptr<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p)=delete;
			//wrong most of the time 
			template<
				typename _OTHER_INDEX_,
				typename _OTHER_PAYLOAD_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr(const ptr_d<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p);

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
			operator value_type*() {return index ? operator->():0;}
			bool operator==(value_type* a)const{return operator->()==a;}
			void _print(ostream& os)const{}
		};
		//specialized for const VALUE_TYPE
		template<
			typename INDEX,
			typename VALUE_TYPE,	
			typename ALLOCATOR,
			typename RAW_ALLOCATOR,
			typename MANAGEMENT
		> struct ptr<INDEX,const VALUE_TYPE,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>{
			typedef cell<INDEX,VALUE_TYPE,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
			INDEX index;
			typedef ptr pointer;
			typedef const VALUE_TYPE value_type;
			typedef value_type element_type;
			typedef const VALUE_TYPE& reference;
			typedef ptrdiff_t difference_type;
			typedef random_access_iterator_tag iterator_category;
			ptr(INDEX index=0):index(index){}
			ptr(const ptr<INDEX,VALUE_TYPE,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& p):index(p.index){}
			//no casting between different types	
			template<
				typename _OTHER_INDEX_,
				typename _OTHER_PAYLOAD_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr(const ptr<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p)=delete;
			//wrong most of the time 
			template<
				typename _OTHER_INDEX_,
				typename _OTHER_PAYLOAD_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr(const ptr_d<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p);
			value_type* operator->()const{
				typedef typename IfThenElse<CELL::OPTIMIZATION,typename CELL::HELPER,CELL>::ResultT PAYLOAD_CELL;
				//should use different call because it return a pointer to const
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
			bool operator==(value_type* a)const{return operator->()==a;}
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
		*	shouldn't it be ptr instead of ptr_d?
		*/ 
		template<
			typename INDEX,
			typename PAYLOAD,
			typename ALLOCATOR,//not needed
			typename RAW_ALLOCATOR,//not needed
			typename MANAGEMENT
		>class cell_iterator{
			INDEX index;
			INDEX cell_index;
		public:
			typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
			typedef cell_iterator pointer;
			typedef PAYLOAD value_type;
			typedef PAYLOAD& reference;
			typedef ptrdiff_t difference_type;
			typedef forward_iterator_tag iterator_category;
			cell_iterator(INDEX index=0):index(_index),cell_index(1){}
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
			operator ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>(){return ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>(get_cell_index());}
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
			cerr<<typeid(T).name()<<"\t"<<tmp<<endl;
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
				return get_impl()->allocate(n);
			}
			void deallocate(pointer p,size_t n){

			}
		};
		typedef ptr<uint8_t,pool,std::allocator<pool>,mmap_allocator<pool>,char> POOL_PTR;//MUST be consistent with POOL_ALLOCATOR definition
		//typedef allocator<uint8_t,pool,std::allocator<pool>,mmap_allocator<pool>,char> POOL_ALLOCATOR;
		template<
			typename _INDEX_,
			typename _PAYLOAD_,
			typename _ALLOCATOR_,
			typename _RAW_ALLOCATOR_,
			typename _MANAGEMENT_
		> struct ptr_d{
			typedef _INDEX_ INDEX;
			typedef _PAYLOAD_ VALUE_TYPE;
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
				typename _OTHER_INDEX_,
				typename _OTHER_PAYLOAD_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr_d(const ptr<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p):pool_ptr(get_pool<typename ptr<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>::CELL>()),index(p.index){
				_OTHER_PAYLOAD_* b=nullptr;
				VALUE_TYPE* a=b;
				#ifdef REF_COUNT
				//how do we know if the pool_ptr is ref counted?
				//derived class is not necessarily ref-counted
				#endif
			}
			//construct from cell_iterator
			template<
				typename _OTHER_INDEX_,
				typename _OTHER_PAYLOAD_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr_d(const cell_iterator<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& c):pool_ptr(get_pool<cell<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>>()),index(c.cell_index){
			}
			template<
				typename _OTHER_INDEX_,
				typename _OTHER_PAYLOAD_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr_d(const ptr_d<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p):pool_ptr(p.pool_ptr),index(p.index){
				//there is not enough information to check the validity of that operation
			}

			template<
				typename _OTHER_INDEX_,
				typename _OTHER_PAYLOAD_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> explicit operator ptr<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>(){
				//VALUE_TYPE* a;
				//_OTHER_PAYLOAD_* b=nullptr;
				//a=b;
				return ptr<_OTHER_INDEX_,_OTHER_PAYLOAD_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>(index);
			}
			value_type* operator->()const{
				//is there any range checking?: no!!!!!, because there is no easy access to management
				typedef cell<_INDEX_,_PAYLOAD_,_ALLOCATOR_,_RAW_ALLOCATOR_,_MANAGEMENT_> CELL;
				return &pool_ptr->get_payload_cast<CELL>(index);
			}
			reference operator*()const{
				typedef cell<_INDEX_,_PAYLOAD_,_ALLOCATOR_,_RAW_ALLOCATOR_,_MANAGEMENT_> CELL;
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
		template<
			typename INDEX,
			typename PAYLOAD,
			typename ALLOCATOR,
			typename RAW_ALLOCATOR,
			typename MANAGEMENT
		> struct allocator{
			//troubleshooting
			typedef RAW_ALLOCATOR _RAW_ALLOCATOR_;
			typedef ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> pointer;
			typedef	ptr<INDEX,const PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> const_pointer; 
			typedef ptr_d<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> derived_pointer;
			typedef ptr_d<INDEX,const PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> const_derived_pointer;
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
				typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				return CELL::MAX_SIZE;
			}
			pointer allocate(size_type n){
				cerr<<"allocate "<<n<<" elements"<<endl;
				typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				return pointer(pool::get_pool<CELL>()->template allocate<CELL>(max<size_t>(n/CELL::FACTOR,1))*CELL::FACTOR);
			}
			pointer allocate_at(INDEX i,size_type n){
				typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				return pointer(pool::get_pool<CELL>()->template allocate_at<CELL>(i,max<size_t>(n/CELL::FACTOR,1))*CELL::FACTOR);
			}
			//what if derived_pointer? should cast but maybe not
			void deallocate(pointer p,size_type n){
				typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				pool::get_pool<CELL>()->template deallocate<CELL>(p.index/CELL::FACTOR,max<size_t>(n/CELL::FACTOR,1));
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
				typedef allocator<INDEX,OTHER_PAYLOAD,ALLOCATOR,OTHER_RAW_ALLOCATOR,MANAGEMENT> other;
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
				typedef cell<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				return pool::get_pool<CELL>()->template get_cells<CELL>()[0].body.info.size;
			}
			//typed iterator
			typedef cell_iterator<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> iterator;
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
		> struct allocator<INDEX,void,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>{
			template<typename OTHER_PAYLOAD> struct rebind{
				typedef typename IfThenElse<
					std::is_same<RAW_ALLOCATOR,std::allocator<char>>::value,
					RAW_ALLOCATOR, 
					typename RAW_ALLOCATOR::template rebind<OTHER_PAYLOAD>::other
				>::ResultT OTHER_RAW_ALLOCATOR;
				typedef allocator<INDEX,OTHER_PAYLOAD,ALLOCATOR,OTHER_RAW_ALLOCATOR,MANAGEMENT> other;
			};
		};
		//
		//let's store pools in a pool...maximum 255 pools for now
		//typedef cell<uint8_t,pool,std::allocator<pool>,std::allocator<char>,char> POOL_CELL;
		typedef cell<uint8_t,pool,std::allocator<pool>,mmap_allocator<pool>,char> POOL_CELL;
		//typedef allocator<POOL_CELL> POOL_ALLOCATOR;
		typedef allocator<uint8_t,pool,std::allocator<pool>,mmap_allocator<pool>,char> POOL_ALLOCATOR;
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
		*
		*/ 
		typedef size_t (*f_ptr)(pool&);
		f_ptr get_size_generic;
		template<typename CELL> static size_t get_size(pool& p){return p.get_cells<CELL>()[0].body.info.size;}
		template<
			typename CELL,
			typename ALLOCATOR=typename CELL::ALLOCATOR,
			typename RAW_ALLOCATOR=typename CELL::RAW_ALLOCATOR
		> struct helper{
			static typename CELL::ALLOCATOR::pointer go(){
				typename CELL::RAW_ALLOCATOR raw;
				size_t cell_size=sizeof(CELL);
				size_t stride=CELL::OPTIMIZATION ? sizeof(typename CELL::PAYLOAD) : cell_size;
				size_t buffer_size=64*cell_size;
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
				a.construct(p,buffer,buffer_size,cell_size,stride,offsetof(CELL,body),str_hash(typeid(typename CELL::PAYLOAD).name()),CELL::MANAGED,f/*pool::get_size<CELL>*/);
				return p;
			}
		};
		/*
		template<
			typename CELL,
			typename ALLOCATOR
		> struct helper<CELL,ALLOCATOR,std::allocator<char>>{


		};
		*/
		//it seems that POOL_ALLOCATOR always use mmap
		template<
			typename CELL,
			typename RAW_ALLOCATOR
		> struct helper<CELL,POOL_ALLOCATOR,RAW_ALLOCATOR>{//specialized for POOL_ALLOCATOR
			static typename CELL::ALLOCATOR::pointer go(){
				/*
				*	maybe the pool has been persisted 
				*/ 
				std::hash<std::string> str_hash;
				//what if we use different allocators with same payload?
				//size_t type_id=str_hash(typeid(typename CELL::PAYLOAD).name());
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
					auto buffer=raw.allocate(buffer_size);
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
					auto p=a.allocate(1);
					if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value)
						cerr<<"create new pool at index "<<(size_t)p.index<<endl;
					else
						cerr<<"create new persistent pool at index "<<(size_t)p.index<<endl;
					f_ptr f=pool::get_size<CELL>;
					//a.construct(p,buffer,buffer_size,sizeof(CELL),sizeof(typename CELL::PAYLOAD),offsetof(CELL,body),type_id,CELL::MANAGED,f/*pool::get_size<CELL>*/);
					a.construct(p,buffer,buffer_size,cell_size,stride,offsetof(CELL,body),type_id,CELL::MANAGED,f/*pool::get_size<CELL>*/);
					return p;
				}else{
					cerr<<"pool found at index "<<(size_t)i.cell_index<<endl;
					typename CELL::RAW_ALLOCATOR raw;
					auto buffer=raw.allocate(buffer_size);
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
					//we need a pointer to the pool	
					//typename CELL::ALLOCATOR::pointer p(i.index);
					typename CELL::ALLOCATOR::pointer p(i);
					//sanity check: has anything changed?
					cerr<<p->cell_size<<" vs "<<cell_size<<endl;
					cerr<<p->payload_offset<<" vs "<<offsetof(CELL,body)<<endl;
					cerr<<p->iterable<<" vs "<<CELL::MANAGED<<endl;
					if(p->cell_size==cell_size&&p->stride==stride&&p->payload_offset==offsetof(CELL,body)&&p->iterable==CELL::MANAGED){
						//we only have to refresh the buffer and function pointers
						p->buffer=buffer;
						p->get_size_generic=pool::get_size<CELL>;
						// we also have to reset buffer_size if not persisted
						if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value)
							p->buffer_size=buffer_size;
						return p;
					}else{
						throw runtime_error("persisted class has been modified");
					}
				}
			}
		};
		//specialize for std::allocator<char*> as the RAW_ALLOCATOR
		template<typename CELL> static typename CELL::ALLOCATOR::pointer create(){return helper<CELL>::go();}
		pool(char* buffer,size_t buffer_size,size_t cell_size,size_t stride,size_t payload_offset,size_t type_id,bool iterable,f_ptr get_size_generic):buffer(buffer),buffer_size(buffer_size),cell_size(cell_size),stride(stride),payload_offset(payload_offset),type_id(type_id),writable(true),iterable(iterable),get_size_generic(get_size_generic){
			cerr<<"new pool "<<(void*)buffer<<endl;
			cerr<<"\tbuffer size:"<<buffer_size<<"\n";
			cerr<<"\tcell size:"<<cell_size<<"\n";
			cerr<<"\tstride:"<<stride<<"\n";
			cerr<<"\tpayload offset:"<<payload_offset<<"\n";
			cerr<<"\ttype id:"<<hex<<type_id<<dec<<"\n";
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
				if((buffer_size+n*cell_size)>(CELL::MAX_SIZE*cell_size)) throw std::bad_alloc();
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
			cerr<<this<<" allocate "<<n<<" cell(s) at index "<<(int)current<<" for "<<typeid(typename CELL::PAYLOAD).name()<<endl;
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
			INDEX index;
			INDEX n;//number of cells to visit
			iterator(POOL_PTR pool_ptr,INDEX index=0,INDEX n=0):pool_ptr(pool_ptr),index(index),n(n){
				if(!pool_ptr->iterable) throw std::runtime_error("pool not iterable");
				while(!pool_ptr->get_cell_cast<CELL>(index).management&&n) ++index;
			}
			iterator& operator++(){
				++index;
				while(!pool_ptr->get_cell_cast<CELL>(index).management&&n) ++index;
				--n;
				return *this;
			}
			//value_type* operator->()const{return &p.get_cell_cast<CELL>(index);}
			//reference operator*()const{return p.get_cell_cast<CELL>(index);}
			value_type* operator->()const{return &pool_ptr->get_payload_cast<CELL>(index);}
			reference operator*()const{return pool_ptr->get_payload_cast<CELL>(index);}
			bool operator==(const iterator& a)const{return n==a.n;}
			bool operator!=(const iterator& a)const{return n!=a.n;}
			template<
				typename INDEX,
				typename PAYLOAD,
				typename ALLOCATOR,
				typename RAW_ALLOCATOR,
				typename MANAGEMENT
			> operator ptr_d<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>(){return ptr_d<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>(pool_ptr,index);} 
		};
		//we need a pool_ptr to itself
		template<typename CELL> static iterator<CELL> begin(POOL_PTR pool_ptr){return iterator<CELL>(pool_ptr,1,pool_ptr->get_size_generic(*pool_ptr));}
		template<typename CELL> static iterator<CELL> end(POOL_PTR pool_ptr){return iterator<CELL>(pool_ptr,1,0);}
		template<typename CELL> static iterator<CELL> cbegin(POOL_PTR pool_ptr){return iterator<CELL>(pool_ptr,1,pool_ptr->get_size_generic(*pool_ptr));}
		template<typename CELL> static iterator<CELL> cend(POOL_PTR pool_ptr){return iterator<CELL>(pool_ptr,1,0);}
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
		typename PAYLOAD_A,
		typename PAYLOAD_B,
		typename ALLOCATOR,
		typename RAW_ALLOCATOR,
		typename MANAGEMENT
	> ptrdiff_t operator-(const pool::ptr<INDEX,PAYLOAD_A,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& a,const pool::ptr<INDEX,PAYLOAD_B,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& b){
		return a.index-b.index;
	}
	//implementation of ptr(const ptr_d&)
	template<typename INDEX,typename PAYLOAD,typename ALLOCATOR,typename RAW_ALLOCATOR,typename MANAGEMENT>
	template<typename OTHER_INDEX,typename OTHER_PAYLOAD,typename OTHER_ALLOCATOR,typename OTHER_RAW_ALLOCATOR,typename OTHER_MANAGEMENT> 
	pool::ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>::ptr(
		const pool::ptr_d<OTHER_INDEX,OTHER_PAYLOAD,OTHER_ALLOCATOR,OTHER_RAW_ALLOCATOR,OTHER_MANAGEMENT>& p
	){
		//not inside assert because compiler complaints
		auto pp=get_pool<typename pool::ptr<INDEX,PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>::CELL>();
		//assert(pp==p.pool_ptr);//what if null pointer?
		assert(pp==p.pool_ptr||p.index==0);
		index=p.index;
	}
	//implementation of ptr(const ptr_d&)
	template<typename INDEX,typename PAYLOAD,typename ALLOCATOR,typename RAW_ALLOCATOR,typename MANAGEMENT>
	template<typename OTHER_INDEX,typename OTHER_PAYLOAD,typename OTHER_ALLOCATOR,typename OTHER_RAW_ALLOCATOR,typename OTHER_MANAGEMENT> 
	pool::ptr<INDEX,const PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>::ptr(
		const pool::ptr_d<OTHER_INDEX,OTHER_PAYLOAD,OTHER_ALLOCATOR,OTHER_RAW_ALLOCATOR,OTHER_MANAGEMENT>& p
	){
		//not inside assert because compiler complaints
		auto pp=get_pool<typename pool::ptr<INDEX,const PAYLOAD,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>::CELL>();
		//assert(pp==p.pool_ptr);//what if null pointer?
		assert(pp==p.pool_ptr||p.index==0);
		index=p.index;
	}

}
//parameter order is different
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> using persistent_allocator_managed=pool_allocator::pool::allocator<
	INDEX,
	_PAYLOAD_,
	pool_allocator::pool::POOL_ALLOCATOR,
	pool_allocator::pool::template mmap_allocator<_PAYLOAD_>,
	bool
>;
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> using persistent_allocator_unmanaged=pool_allocator::pool::allocator<
	INDEX,
	_PAYLOAD_,
	pool_allocator::pool::POOL_ALLOCATOR,
	pool_allocator::pool::template mmap_allocator<_PAYLOAD_>,
	void
>;
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> using volatile_allocator_managed=pool_allocator::pool::allocator<
	INDEX,
	_PAYLOAD_,
	pool_allocator::pool::POOL_ALLOCATOR,
	std::allocator<char>,
	bool
>;
#ifdef REF_COUNT
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> using volatile_allocator_managed_rc=pool_allocator::pool::allocator<
	INDEX,
	_PAYLOAD_,
	pool_allocator::pool::POOL_ALLOCATOR,
	std::allocator<char>,
	uint8_t
>;

#endif
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t
> using volatile_allocator_unmanaged=pool_allocator::pool::allocator<
	INDEX,
	_PAYLOAD_,
	pool_allocator::pool::POOL_ALLOCATOR,
	std::allocator<char>,
	void
>;
template<
	typename _PAYLOAD_
> struct singleton_allocator{

};
#endif
