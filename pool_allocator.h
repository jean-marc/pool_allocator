#ifndef POOL_ALLOCATOR_H
#define POOL_ALLOCATOR_H
/*
 *	simplest allocator that works
 *
 */
#ifndef NO_MMAP
#endif
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
#include <experimental/string_view>
#ifdef POOL_ALLOCATOR_THREAD_SAFE
#include <mutex>
#endif
#include "ifthenelse.hpp"
namespace pool_allocator{
	extern int verbosity;
	extern const char _context_[];
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
		static const size_t MAX_SIZE=std::numeric_limits<INDEX>::max();
		static const size_t MAX_BUFFER_SIZE=MAX_SIZE;//+1;//what if MAX_SIZE+1=0 that is INDEX=size_t
		static const INDEX max_index=std::numeric_limits<INDEX>::max();//max_index and MAX_SIZE are the same because cell 0 is off-limit
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
		static void check(const cell& c,INDEX index){
			if(!c.management) throw std::out_of_range(std::string("bad reference ")+std::to_string(index)+" "+typeid(PAYLOAD).name());	
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
		static const size_t MAX_BUFFER_SIZE=(1ULL<<(sizeof(INDEX)<<3))/FACTOR;
		static const size_t MAX_SIZE=std::numeric_limits<INDEX>::max()/FACTOR-1;
		static const INDEX max_index=std::numeric_limits<INDEX>::max()/FACTOR;
		static void post_allocate(cell*,cell*){}
		static void post_deallocate(cell*,cell*){}
		static void is_available(cell* begin,cell* end){}
		static void check(const cell& c,INDEX){}
		#ifdef REF_COUNT
		static void increase_ref_count(cell& c){}
		static void decrease_ref_count(cell& c){}
		static int get_ref_count(cell& c){return 0;}
		#endif
	};
	//trigger when pool loaded from memory
	/*
	template<typename PAYLOAD> struct pool_loaded{
		static void go(){LOG_NOTICE<<"pool loaded from memory"<<endl;}
	};
	*/
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
			typedef std::random_access_iterator_tag iterator_category;
			ptr(std::nullptr_t=nullptr):index(0){}
			//create ambiguity when ptr(0) add parameter to disambiguate
			explicit ptr(INDEX index,int):index(index){}
			#ifdef REF_COUNT
			ptr(const ptr& p):index(p.index){
				if(is_same<MANAGEMENT,uint8_t>::value){	
					LOG_DEBUG<<"copy constructor"<<std::endl;	
					//LOG<<"management:"<<(int)pool::get_pool<CELL>()->get_cell_cast<CELL>(index).management<<endl;
					LOG_DEBUG<<"management:"<<CELL::get_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index))<<std::endl;
					CELL::increase_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index)); 
				}
			}
			ptr& operator=(const ptr& p){
				if(is_same<MANAGEMENT,uint8_t>::value){	
					LOG_DEBUG<<"copy operator"<<std::endl;	
					if(index){
						//if(pool::get_pool<CELL>()->get_cell_cast<CELL>(index).management==1){
						if(CELL::get_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index))==1){
							(*this)->~VALUE_TYPE();
							allocator<VALUE_TYPE,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> a;
							pool::get_pool<CELL>()->template deallocate<CELL>(index/CELL::FACTOR,std::max<size_t>(1/CELL::FACTOR,1));
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
					LOG_DEBUG<<"~ptr"<<std::endl;
					//LOG<<"management:"<<(int)pool::get_pool<CELL>()->get_cell_cast<CELL>(index).management<<endl;
					LOG_DEBUG<<"management:"<<CELL::get_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index))<<endl;
					if(index){
						//if(pool::get_pool<CELL>()->get_cell_cast<CELL>(index).management==1){
						if(CELL::get_ref_count(pool::get_pool<CELL>()->get_cell_cast<CELL>(index))==1){
							(*this)->~VALUE_TYPE();
							allocator<VALUE_TYPE,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> a;
							pool::get_pool<CELL>()->template deallocate<CELL>(index/CELL::FACTOR,std::max<size_t>(1/CELL::FACTOR,1));
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
				LOG<<"~~~"<<(void*)pool::get_pool<CELL>()->buffer<<"\t"<<(void*)p<<endl;			
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
				if(!index) throw std::runtime_error(std::string("null reference for ")+typeid(VALUE_TYPE).name());	
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
			bool operator<=(const ptr& a)const{return index<=a.index;}
			bool operator>(const ptr& a)const{return index>a.index;}
			bool operator>=(const ptr& a)const{return index>=a.index;}
			/*
 			*  template <typename FancyPtr>
 			*  auto trueaddress(FancyPtr ptr) {  
 			*  	return !ptr ? nullptr : std::addressof(*ptr); 
 			*  }
 			*  see https://rawgit.com/google/cxx-std-draft/allocator-paper/allocator_user_guide.html
 			*/
			//make cast operators explicit otherwise ambiguous calls to operator== and +
			explicit operator bool() const{return index;}
			#ifdef FIX_AMBIGUITY
			explicit operator value_type*(){return index ? operator->():0;}
			explicit operator const value_type*() const{return index ? operator->():0;}
			#else
			operator value_type*(){return index ? operator->():0;}
			operator const value_type*() const{return index ? operator->():0;}
			#endif
			void _print(std::ostream& os)const{}
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
			typedef std::random_access_iterator_tag iterator_category;
			ptr(std::nullptr_t p=nullptr):index(0){}
			ptr(INDEX index,int):index(index){}
			ptr(const ptr<const VALUE_TYPE,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>& p):index(p.index){}
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
			//cast operator
			explicit operator bool() const{return index;}
			#ifdef FIX_AMBIGUITY
			explicit operator value_type*(){return index ? operator->():0;}
			#else
			operator value_type*(){return index ? operator->():0;}
			#endif
			void _print(std::ostream& os)const{}
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
			typedef std::forward_iterator_tag iterator_category;
			cell_iterator(INDEX index=0):index(index),cell_index(1){
				if(index<pool::get_pool<CELL>()->template get_cells<CELL>()[0].body.info.size){
					while(!pool::get_pool<CELL>()->template get_cells<CELL>()[cell_index].management) ++cell_index;
				}
			}
			cell_iterator& operator++(){
				++index;
				++cell_index;//otherwise always stays on same cell
				if(index<pool::get_pool<CELL>()->template get_cells<CELL>()[0].body.info.size){
					while(!pool::get_pool<CELL>()->template get_cells<CELL>()[cell_index].management) ++cell_index;
				}
				return *this;
			}
			value_type* operator->() const{return &pool::get_pool<CELL>()->template get_cells<CELL>()[cell_index].body.payload;}	
			reference operator*() const{return pool::get_pool<CELL>()->template get_cells<CELL>()[cell_index].body.payload;}
			bool operator==(const cell_iterator& a)const{return index==a.index;}
			bool operator<(const cell_iterator& a)const{return index<a.index;}
			bool operator!=(const cell_iterator& a)const{return index!=a.index;}
			INDEX get_cell_index() const{return cell_index;}
			operator ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>(){return ptr<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>(get_cell_index(),0);}
		};
		#ifndef NO_MMAP
		/*
		*	allocate memory on file
		*/ 
		struct mmap_allocator_impl{
			int fd;
			bool writable;
			void* v;
			size_t file_size;
			enum{PAGE_SIZE=4096};
			mmap_allocator_impl(std::string filename):writable(true){
				LOG_NOTICE<<"opening file `"<<filename<<"' O_RDWR"<<std::endl;
				fd = open(filename.c_str(), O_RDWR | O_CREAT/* | O_TRUNC*/, (mode_t)0600);
				if(fd ==-1){
					LOG_NOTICE<<"opening file `"<<filename<<"' O_RDONLY"<<std::endl;
					fd = open(filename.c_str(), O_RDONLY/* | O_TRUNC*/, (mode_t)0600);
					writable=false;
				}
				if (fd == -1) {
					LOG_ERROR<<"\nError opening file `"<<filename<<"' for writing or reading"<<std::endl;
					exit(EXIT_FAILURE);
				}
				//set the size
				struct stat s;
				int r=fstat(fd,&s);
				if(r==-1){
					LOG_ERROR<<"\ncould not stat file `"<<filename<<"'"<<std::endl;
					exit(EXIT_FAILURE);
				}
				file_size=0;
				if(s.st_size==0){
					//new file
					file_size=PAGE_SIZE;
					int result = lseek(fd,file_size-1, SEEK_SET);
					if (result == -1) {
						close(fd);
						LOG_ERROR<<"Error calling lseek() to 'stretch' the file"<<std::endl;
						exit(EXIT_FAILURE);
					}
					result = write(fd, "", 1);
					if (result != 1) {
						close(fd);
						LOG_ERROR<<"Error writing last byte of the file"<<std::endl;
						exit(EXIT_FAILURE);
					}
				}else{
					file_size=s.st_size;
				}
				v = writable ? mmap((void*)NULL,file_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0) : mmap((void*)NULL,file_size,PROT_READ,MAP_SHARED,fd,0);
				LOG_NOTICE<<"new mapping at "<<v<<" size:"<<file_size<<std::endl;
				if (v == MAP_FAILED) {
					close(fd);
					LOG_ERROR<<"Error mmapping the file"<<std::endl;
					exit(EXIT_FAILURE);
				}
			}
			//it is not a proper allocator, can we make it a proper allocator so we can easily swap?
			char* allocate(size_t n){
				LOG_DEBUG<<"mmap_allocator::allocate()"<<std::endl;
				if(n>file_size){
					size_t _file_size=std::max<size_t>(ceil((double)(n)/PAGE_SIZE),1)*PAGE_SIZE;
					int result = lseek(fd,_file_size-1, SEEK_SET);
					if (result == -1) {
						close(fd);
						LOG_ERROR<<"Error calling lseek() to 'stretch' the file"<<std::endl;
						exit(EXIT_FAILURE);
					}
					result = write(fd, "", 1);
					if (result != 1) {
						close(fd);
						LOG_ERROR<<"Error writing last byte of the file"<<std::endl;
						exit(EXIT_FAILURE);
					}
					void* _v=(char*)mremap(v,file_size,_file_size,MAP_SHARED,MREMAP_MAYMOVE);
					if (_v == MAP_FAILED) {
						close(fd);
						LOG_ERROR<<"Error mremapping the file"<<std::endl;
						exit(EXIT_FAILURE);
					}
					v=_v;
					LOG_NOTICE<<"new mapping at "<<v<<" size:"<<_file_size<<std::endl;
					file_size=_file_size;
				}
				LOG_DEBUG<<"mmap_allocator::allocate "<<v<<std::endl;
				return (char*)v;
			}
		};
		#endif
		template<typename T> static size_t get_hash(){
			std::hash<std::string> str_hash;
			auto tmp=str_hash(typeid(T).name());
			//LOG<<typeid(T).name()<<"\t"<<tmp<<endl;
			//return str_hash(typeid(T).name());
			return tmp;
		}
		template<typename T> struct file_name{
			static std::string get(){
				/* 
 				* use the hash of the typeid in case names are too long or not valid file names 
				*/
				std::ostringstream os;
				os<<std::setfill('0')<<std::hex<<std::setw(16)<<get_hash<T>();
				return os.str();
			}
			//let's have a rebind 
			template<typename OTHER_T> struct rebind{
				typedef file_name<OTHER_T> other;
			};
		};
		#ifndef NO_MMAP
		//confusing T is not the type allocated, just a unique type used for identification
		template<
			typename T,
			typename FILE_NAME=file_name<T>
		> struct mmap_allocator{
			template<typename OTHER_PAYLOAD> struct rebind{
				typedef mmap_allocator<OTHER_PAYLOAD,FILE_NAME> other;
			};
			typedef char* pointer;
			//typedef T value_type;
			bool writable;
			static mmap_allocator_impl* get_impl(){
				//bind as late as possible otherwise compiler complaints
				static mmap_allocator_impl* a=new mmap_allocator_impl(std::string("db/")+FILE_NAME::template rebind<T>::other::get());
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
		#else
		template<
			typename T,
			typename FILE_NAME=file_name<T>
		> struct mmap_allocator:std::allocator<char>{
			bool writable=true;
			char* allocate(size_t n){
				LOG_DEBUG<<"mmap_allocator::allocate("<<n<<")"<<std::endl;
				return std::allocator<char>::allocate(n);
			}
			void deallocate(char* p,size_t n){
				LOG_DEBUG<<"mmap_allocator::deallocate("<<p<<","<<n<<")"<<std::endl;
			}
		};
		#endif
		typedef ptr<pool,uint8_t,std::allocator<pool>,mmap_allocator<pool>,char> POOL_PTR;//MUST be consistent with POOL_ALLOCATOR definition
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
			typedef std::random_access_iterator_tag iterator_category;
			ptr_d(std::nullptr_t):pool_ptr(nullptr),index(0){}
			ptr_d(POOL_PTR pool_ptr=nullptr,INDEX index=0):pool_ptr(pool_ptr),index(index){}
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
			template<
				typename _OTHER_PAYLOAD_,
				typename _OTHER_INDEX_,
				typename _OTHER_ALLOCATOR_,
				typename _OTHER_RAW_ALLOCATOR_,
				typename _OTHER_MANAGEMENT_
			> ptr_d(const ptr_d<_OTHER_PAYLOAD_,_OTHER_INDEX_,_OTHER_ALLOCATOR_,_OTHER_RAW_ALLOCATOR_,_OTHER_MANAGEMENT_>& p):pool_ptr(p.pool_ptr),index(p.index){
				_OTHER_PAYLOAD_* b=nullptr;
				//more work needed here when up-casting
				//VALUE_TYPE* a=b;
				//there is not enough information to check the validity of that operation
				//but there should be a relation-ship between PAYLOADS...
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

			/*
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
			*/
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
			bool operator<(const ptr_d& a)const{
				if(pool_ptr==a.pool_ptr) return index<a.index;
				return pool_ptr<a.pool_ptr;
			}
			//cast operator
			explicit operator bool() const{return index;}
			#ifdef FIX_AMBIGUITY
			//what about casting to void*???
			explicit operator value_type*(){return index ? operator->():0;}
			explicit operator const value_type*() const{return index ? operator->():0;}
			#else
			operator value_type*(){return index ? operator->():0;}
			operator const value_type*() const{return index ? operator->():0;}
			#endif
			void _print(std::ostream& os)const{}
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
			#ifdef POOL_ALLOCATOR_THREAD_SAFE
			static std::mutex m;
			#endif
			allocator(){}
			~allocator(){}
			//what is the problem with this?
			typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
			//this is a upper boundary of the maximum size 
			size_type max_size() const throw(){
				typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				return CELL::MAX_SIZE;
			}
			//we have to introduce thread-safety!
			pointer allocate(size_type n){
				#ifdef POOL_ALLOCATOR_THREAD_SAFE
				std::lock_guard<std::mutex> lock(m);
				#endif
				LOG_DEBUG<<"allocate "<<n<<" elements"<<std::endl;
				typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				return pointer(pool::get_pool<CELL>()->template allocate<CELL>(std::max<size_t>(ceil(1.0*n/CELL::FACTOR),1))*CELL::FACTOR,0);
			}
			pointer allocate_at(INDEX i,size_type n){
				#ifdef POOL_ALLOCATOR_THREAD_SAFE
				std::lock_guard<std::mutex> lock(m);
				#endif
				typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				return pointer(pool::get_pool<CELL>()->template allocate_at<CELL>(i,std::max<size_t>(ceil(1.0*n/CELL::FACTOR),1))*CELL::FACTOR,0);
			}
			//will wrap when pointer reaches last
			pointer ring_allocate(INDEX last){
				typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				auto tmp=allocate(1);
				//now make sure there is a cell available for next allocation by deallocating next cell
				if(size()==last){//the next allocation should be at 1
					auto next=tmp+1;
					if(!next.index || next.index > last) next.index=1;
					//why doesn't this compile???????
					//if(next==nullptr) ++next;
					LOG_DEBUG<<"deallocate cell "<<(int)next.index<<std::endl;
					deallocate(next,1);
					LOG_DEBUG<<"new size:"<<size()<<std::endl;
				}
				return tmp;
			}
			//what if derived_pointer? should cast but maybe not
			void deallocate(pointer p,size_type n){
				#ifdef POOL_ALLOCATOR_THREAD_SAFE
				std::lock_guard<std::mutex> lock(m);
				#endif
				typedef cell<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT> CELL;
				pool::get_pool<CELL>()->template deallocate<CELL>(p.index/CELL::FACTOR,std::max<size_t>(ceil(1.0*n/CELL::FACTOR),1));
			}
			//if this function is needed it means the container does not use the pointer type and persistence will fail
			/*void deallocate(value_type* p,size_type n){

			}*/
			//we have to rebind the RAW_ALLOCATOR as well!!!!! except if std::allocator, 
			//why can't we rebind std::allocator: because we always use std::allocator<char>, mmap_allocator needs a type to 
			//get unique file
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
			//could also specialize std::hash<allocator> but maybe confusing
			static size_t get_hash(){
				auto p=get_pool();
				std::experimental::string_view str(p->buffer,p->buffer_size);
				return std::hash<std::experimental::string_view>{}(str);
			}
			template<typename... Args> void construct(pointer p,Args... args){
				LOG_DEBUG<<"construct at "<<(int)p.index<<"("<<(void*)p.operator->()<<")"<<std::endl;
				#ifdef FIX_AMBIGUITY
				new((PAYLOAD*)p) value_type(args...);
				#else
				new(p) value_type(args...);
				#endif
			}
			void destroy(pointer p){
				LOG_DEBUG<<"destroy at "<<(int)p.index<<std::endl;
				p->~value_type();
			}
			//this might cause problem, is the pointer still valid? it should be because when using vector a whole block is allocated
			/*
			template<typename... Args> void construct(value_type* p,Args... args){
				LOG<<"construct at "<<(void*)p<<endl;
				new(p) value_type(args...);
			}
			void destroy(value_type* p){
				LOG<<"destroy at "<<(void*)p<<endl;
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
			//would be nice if end iterator would be cast to null pointer? does it make sense?
			iterator end(){return iterator(size());}
			const_iterator cbegin(){return const_iterator();}
			const_iterator cend(){return const_iterator(size());}
			//experimental, UNSAFE!!!
			PAYLOAD& operator[](size_t index){
				return *pointer(index,0);
			}
			
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
		};

		//specialize for void
		template<
			typename INDEX,
			typename ALLOCATOR,
			typename RAW_ALLOCATOR,
			typename MANAGEMENT
		> struct allocator<void,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>{
			typedef RAW_ALLOCATOR _RAW_ALLOCATOR_;
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
				size_t buffer_size=128*cell_size;
				typename CELL::ALLOCATOR a;
				//LOG<<"looking for pool `"<<typeid(typename CELL::PAYLOAD).name()<<"'"<<endl;
				LOG_DEBUG<<"looking for pool `"<<typeid(CELL).name()<<"'\t"<<std::hex<<type_id<<std::dec<<"\t"<<a.size()<<std::endl;
				//what if not iterable?
				auto i=std::find_if(a.cbegin(),a.cend(),[=](const pool& p){return p.type_id==type_id;});
				if(i==a.cend()){
					LOG_NOTICE<<"pool not found"<<std::endl;
					typename CELL::RAW_ALLOCATOR raw;//what is the payload?
					//LOG<<"RAW_ALLOCATOR:"<<typeid(typename CELL::RAW_ALLOCATOR::value_type).name()<<endl;
					//we could simplify a lot by giving filename to allocator
					auto buffer=raw.allocate(buffer_size);//should specialize so we can 
					if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value){
						LOG_NOTICE<<"resetting volatile memory"<<std::endl;
						memset(buffer,0,buffer_size);
					}
					CELL *c=(CELL*)buffer;
					if(c[0].body.info.size==0&&c[0].body.info.next==0){
						LOG_NOTICE<<"resetting the buffer"<<std::endl;
						c[0].body.info.size=0;//new pool
						c[0].body.info.next=1;
						c[1].body.info.size=buffer_size/sizeof(CELL)-1;
						c[1].body.info.next=0;
					}
					/*
 					*	warn if allocator uses local copy	
 					*	it is more serious than that: if the main pool is read-only all the other pools must be made read-only as well!
 					*/
					if(!pool::template get_pool<POOL_CELL>()->writable) LOG_WARNING<<"Warning: change only made to local copy!"<<std::endl;
					auto p=a.allocate(1);
					if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value)
						LOG_NOTICE<<"create new pool at index "<<(size_t)p.index<<std::endl;
					else
						LOG_NOTICE<<"create new persistent pool at index "<<(size_t)p.index<<std::endl;
					f_ptr f=pool::get_size<CELL>;
					a.construct(p,buffer,buffer_size,cell_size,stride,offsetof(CELL,body),type_id,true,CELL::MANAGED,f/*pool::get_size<CELL>*/);
					return p;
				}else{
					LOG_NOTICE<<"pool found at index "<<(size_t)i.cell_index<<std::endl;
					typename CELL::RAW_ALLOCATOR raw;
					auto buffer=raw.allocate(buffer_size);//at this stage we know if it is writable or not
					if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value){
						LOG_NOTICE<<"resetting volatile memory"<<std::endl;
						memset(buffer,0,buffer_size);
					}
					CELL *c=(CELL*)buffer;
					if(c[0].body.info.size==0&&c[0].body.info.next==0){//also used if file has been deleted
						LOG_NOTICE<<"resetting the buffer"<<std::endl;
						c[0].body.info.size=0;//new pool
						c[0].body.info.next=1;
						c[1].body.info.size=buffer_size/sizeof(CELL)-1;
						c[1].body.info.next=0;
					}
					//we need a pointer to the pool	
					typename CELL::ALLOCATOR::pointer p(i);
					//sanity check: has anything changed?
					LOG_DEBUG<<p->cell_size<<" vs "<<cell_size<<std::endl;
					LOG_DEBUG<<p->payload_offset<<" vs "<<offsetof(CELL,body)<<std::endl;
					LOG_DEBUG<<p->iterable<<" vs "<<CELL::MANAGED<<std::endl;
					if(p->cell_size==cell_size&&p->stride==stride&&p->payload_offset==offsetof(CELL,body)&&p->iterable==CELL::MANAGED){
						/*
 						*	this is a problem if multiple processes use the same db: the last
 						*	process started will cause segfault in running process, is there anywhere
 						*	else we can store that data?
 						*	It would also be nice to be able to make the database read-only for testing purpose
 						*/
						//we only have to refresh the buffer and function pointers
						LOG_NOTICE<<"modifying pool struct..."<<std::endl;
						p->buffer=buffer;//this will segfault if mounted read-only, what can we do????
						p->get_size_generic=pool::get_size<CELL>;
						// we also have to reset buffer_size if not persisted
						//invoke trigger, the problem is that 
						//pool_loaded<PAYLOAD>::go();//shall we pass some information?
						if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value)
							p->buffer_size=buffer_size;
						return p;//could we return a different pointer? dangerous: the index is used to allocate rdfs::Class
					}else{
						throw std::runtime_error("persisted class has been modified");
					}
				}
			}
		};
		/*
 		*	specialized for pool of pools
 		*
 		*/ 
		template<
			typename CELL
		> struct helper<CELL,pool>{
			static typename CELL::ALLOCATOR::pointer go(){
				LOG_DEBUG<<"pool of pools"<<std::endl;
				std::hash<std::string> str_hash;
				size_t type_id=str_hash(typeid(pool).name());//use the payload instead of cell type for consistency with filename
				size_t cell_size=sizeof(CELL);
				size_t stride=CELL::OPTIMIZATION ? sizeof(typename CELL::PAYLOAD) : cell_size;
				#ifdef NO_MMAP
				/*
 				*	I don't understand why it needs to be bigger than 256? to be investigated
 				*/ 
				size_t buffer_size=16*128*cell_size;//this is dangerous because the file_size might be bigger!
				#else
				size_t buffer_size=128*cell_size;//this is dangerous because the file_size might be bigger!
				#endif
				typename CELL::RAW_ALLOCATOR raw;
				auto buffer=raw.allocate(buffer_size);//what about allocating CELL's instead of char?, it would have the advantage of aligning the data
				if(!raw.writable){
					LOG_NOTICE<<"copying memory-mapped file to RAM"<<std::endl;
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
			LOG_NOTICE<<"new pool "<<(void*)buffer<<std::endl;
			LOG_DEBUG<<"\tbuffer size:"<<buffer_size<<"\n";
			LOG_DEBUG<<"\tcell size:"<<cell_size<<"\n";
			LOG_DEBUG<<"\tstride:"<<stride<<"\n";
			LOG_DEBUG<<"\tpayload offset:"<<payload_offset<<"\n";
			LOG_DEBUG<<"\ttype id:"<<std::hex<<type_id<<std::dec<<"\n";
			LOG_DEBUG<<"\twritable:"<<writable<<"\n";
			LOG_DEBUG<<"\titerable:"<<iterable<<"\n";
		}
		~pool(){
			LOG_DEBUG<<"~pool()"<<std::endl;
		}
		template<typename CELL> CELL* get_cells(){
			return (CELL*)buffer;
		}
		//size in cells
		size_t size() const{return buffer_size/cell_size;}
		template<typename CELL> void status(){
			CELL *c=(CELL*)buffer;
			LOG_DEBUG<<"pool "<<c[0].body.info.size<<"/"<<buffer_size/sizeof(CELL)<<" cell(s) "<<std::endl;
		}
		//should only allocate 1 cell at a time, must not be mixed with allocate()!
		//why can't it be mixed allocate? that could be useful
		template<typename CELL> typename CELL::INDEX allocate_at(typename CELL::INDEX i,size_t n){
			/*
 			*	do we have to grow the pool?
 			*/	 
			if(buffer_size<(i+n-1)*cell_size){
				if((i+n-1)>CELL::max_index){
					std::cerr<<"CELL::max_index:"<<CELL::max_index<<std::endl;	
					throw std::bad_alloc();
				}
				size_t new_buffer_size=(i+n)*cell_size;
				LOG_NOTICE<<this<<" increasing pool size from "<<buffer_size<<" to "<<new_buffer_size<<std::endl;
				typename CELL::RAW_ALLOCATOR raw;
				auto new_buffer=raw.allocate(new_buffer_size);
				//the next 3 stages must be avoided when dealing with mmap
				if(std::is_same<typename CELL::RAW_ALLOCATOR,std::allocator<char>>::value){
					memcpy(new_buffer,buffer,buffer_size);
					memset(new_buffer+buffer_size,0,new_buffer_size-buffer_size);
					raw.deallocate(buffer,buffer_size);	
				}
				buffer=new_buffer;
				buffer_size=new_buffer_size;
			}
			CELL *c=(CELL*)buffer;
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
			display<CELL>();
			CELL *c=(CELL*)buffer;
			typedef typename CELL::INDEX INDEX;
			INDEX prev=0,current=c[prev].body.info.next;
			//LOG<<"current: "<<(int)current<<endl;
			//this should be made thread-safe
			while(current && c[current].body.info.size<n){
				LOG_DEBUG<<"\t"<<(int)current<<std::endl;
				prev=current;
				current=c[prev].body.info.next;
			}
			//could we have an atomic variable that tells us if the cell is actually free?
			if(current){ //we have found enough contiguous cells
				if(c[current].body.info.size==n){
					//LOG<<"found!"<<(int)prev<<"\t"<<(int)current<<"\t"<<(int)c[current].body.info.next<<endl;
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
				if(prev && prev+c[prev].body.info.size==buffer_size/cell_size){
					LOG_DEBUG<<"last cell!"<<std::endl;
					new_size=n-c[prev].body.info.size;
				}else{
					new_size=n;
				}
				LOG_NOTICE<<"new buffer size:"<<(buffer_size/cell_size)+new_size<<" vs "<<(CELL::MAX_BUFFER_SIZE)<<std::endl;
				if(((buffer_size/cell_size)+new_size)>(CELL::MAX_BUFFER_SIZE)) throw std::bad_alloc();
				size_t new_buffer_size=buffer_size+new_size*cell_size;
				/*
				#ifdef OPTIM_POS
				//grow last region	
				LOG<<"upper boundary:"<<prev+n<<endl;
				if((prev+n)>CELL::MAX_SIZE) throw std::bad_alloc();
				#else
				LOG<<"new buffer size:"<<buffer_size+n*cell_size<<"\t"<<CELL::MAX_SIZE*cell_size<<endl;
				if((buffer_size+n*cell_size)>(CELL::MAX_SIZE*cell_size)) throw std::bad_alloc();
				#endif
				size_t new_buffer_size=min<size_t>(max<size_t>(buffer_size+n*cell_size,2*buffer_size),CELL::MAX_SIZE*cell_size);
				*/
				LOG_NOTICE<<this<<" increasing pool size from "<<buffer_size<<" to "<<new_buffer_size<<std::endl;
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
				if(buffer_size/cell_size==CELL::MAX_BUFFER_SIZE){//pool is full
					c[0].body.info.size=CELL::MAX_BUFFER_SIZE-1;
					c[0].body.info.next=0;
				}else{
					current=buffer_size/cell_size;	
					c[current].body.info.size=(new_buffer_size-buffer_size)/cell_size;
					c[current].body.info.next=0;
					c[prev].body.info.next=current;	
					//connect
					if(prev && prev+c[prev].body.info.size==current){
						c[prev].body.info.size+=c[current].body.info.size;
						c[prev].body.info.next=c[current].body.info.next;
					}
				}
				#else
				c[buffer_size/cell_size].body.info.size=(new_buffer_size-buffer_size)/cell_size;
				c[buffer_size/cell_size].body.info.next=c[0].body.info.next;
				c[0].body.info.next=buffer_size/cell_size;	
				#endif
				buffer_size=new_buffer_size;
				return allocate<CELL>(n);
			}
			LOG_DEBUG<<this<<" allocate "<<n<<" cell(s) at index "<<(int)current<<" for "<<std::hex<<typeid(CELL).name()<<std::dec<<std::endl;
			return current;
		}	
		template<typename CELL> void display() const{
			CELL *c=(CELL*)buffer;
			typedef typename CELL::INDEX INDEX;
			INDEX prev=0,current=c[prev].body.info.next;
			while(current){
				LOG_DEBUG<<(int)current<<"\t"<<(int)c[current].body.info.size<<"\t"<<(int)c[current].body.info.next<<std::endl;
				prev=current;
				current=c[prev].body.info.next;
			}
		}
		template<typename CELL> void deallocate(typename CELL::INDEX index,size_t n){
			LOG_DEBUG<<this<<" deallocate "<<n<<" cell(s) at index "<<(int)index<<std::endl;
			CELL *c=(CELL*)buffer;
			typedef typename CELL::INDEX INDEX;
			display<CELL>();
			#ifdef OPTIM_POS
			/*
 			*	we should insert at right position and connect adjacent regions
 			*/ 
			INDEX prev=0,current=c[prev].body.info.next;
			while(current && current<index){
				LOG_DEBUG<<"\t"<<(int)current<<std::endl;
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
			LOG_DEBUG<<"deallocate: prev,current {"<<(int)prev<<","<<(int)current<<"}"<<std::endl;
			if(current){
				if(prev && (prev+c[prev].body.info.size==index)){//connected to prev
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
			}else{//at end of array
				if(prev && prev+c[prev].body.info.size==index){//connected to prev
					c[prev].body.info.size+=n;
					c[prev].body.info.next=current;
				}else{
					c[index].body.info.size=n;
					c[index].body.info.next=current;
					c[prev].body.info.next=index;
				}
			}
			c[0].body.info.size-=n;//update total number of cells in use
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
			//LOG<<this<<" dereference cell at index "<<(int)index<<endl;
			CELL *c=(CELL*)buffer;
			//what if buffer gets modified here because of pool increase?
			CELL::check(c[index],index);//bounds checking
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
			//LOG<<"management?:"<<payload_offset<<"\t"<<(int)(uint8_t)(*p)<<endl;
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
			//can we add a callback here?
			//CELL::PAYLOAD
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
			typedef std::forward_iterator_tag iterator_category;
			POOL_PTR pool_ptr;
			INDEX cell_index;
			INDEX index;
			iterator(POOL_PTR pool_ptr,INDEX index=0):pool_ptr(pool_ptr),index(index),cell_index(1){
				if(!pool_ptr->iterable) throw std::runtime_error("pool not iterable");
				if(index<pool_ptr->get_size_generic(*pool_ptr)){
					while(!pool_ptr->get_cell_cast<CELL>(cell_index).management) ++cell_index;
				}
			}
			iterator& operator++(){
				++index;
				++cell_index;//otherwise always stays on same cell
				if(index<pool_ptr->get_size_generic(*pool_ptr)){
					while(!pool_ptr->get_cell_cast<CELL>(cell_index).management) ++cell_index;
				}
				return *this;
			}
			value_type* operator->() const{return &pool_ptr->get_payload_cast<CELL>(cell_index);}
			reference operator*() const{return pool_ptr->get_payload_cast<CELL>(cell_index);}
			bool operator==(const iterator& a)const{return index==a.index;}
			bool operator!=(const iterator& a)const{return index!=a.index;}
			bool operator<(const iterator& a)const{return index<a.index;}
			INDEX get_cell_index() const{return cell_index;}
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
	#ifdef POOL_ALLOCATOR_THREAD_SAFE
	template<
		typename PAYLOAD,
		typename INDEX,
		typename ALLOCATOR,
		typename RAW_ALLOCATOR,
		typename MANAGEMENT
	> 	std::mutex pool::allocator<PAYLOAD,INDEX,ALLOCATOR,RAW_ALLOCATOR,MANAGEMENT>::m;
	#endif

}
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t,
	typename FILE_NAME=pool_allocator::pool::file_name<_PAYLOAD_>
> using persistent_allocator_managed=pool_allocator::pool::allocator<
	_PAYLOAD_,
	INDEX,
	pool_allocator::pool::POOL_ALLOCATOR,
	pool_allocator::pool::template mmap_allocator<_PAYLOAD_,FILE_NAME>,
	bool
>;
template<
	typename _PAYLOAD_,
	typename INDEX=uint8_t,
	typename FILE_NAME=pool_allocator::pool::file_name<_PAYLOAD_>
> using persistent_allocator_unmanaged=pool_allocator::pool::allocator<
	_PAYLOAD_,
	INDEX,
	pool_allocator::pool::POOL_ALLOCATOR,
	pool_allocator::pool::template mmap_allocator<_PAYLOAD_,FILE_NAME>,
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
