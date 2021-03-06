CC = g++ -I .  
CFLAGS = -O3 -std=c++17 -UREF_COUNT -lpthread -UOPTIM_POS -DFIX_AMBIGUITY -DLOG_DEBUG=std::cerr -DLOG_NOTICE=std::cerr -DLOG_ERROR=std::cerr -DLOG_WARNING=std::cerr
%.o:%.cpp %.h
	$(CC) -c $(CFLAGS) $< -o $@
test%:test%.cpp pool_allocator.h
	$(CC) $(CFLAGS) $< -o $@ 
empty:

#install:pool_allocator.h
	#mkdir -p /usr/local/include/pool_allocator
	#cp pool_allocator.h ifthenelse.hpp /usr/local/include/pool_allocator/
#arm_install:pool_allocator.h
#	cp pool_allocator.h ifthenelse.hpp /opt/ioplex_mx/usr/arm-buildroot-linux-gnueabihf/sysroot/usr/include/pool_allocator
prefix=/usr
exec_prefix=$(prefix)
includedir=$(prefix)/include
libdir=$(exec_prefix)/lib
INSTALL=install
INSTALL_PROGRAM=$(INSTALL)
INSTALL_DATA=$(INSTALL) -m 644
install:
	mkdir -p $(DESTDIR)$(includedir)/pool_allocator
	$(INSTALL_DATA) pool_allocator.h ifthenelse.hpp $(DESTDIR)$(includedir)/pool_allocator
check:
	echo 'it is all good!'
