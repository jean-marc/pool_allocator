CC = g++ -I .  
CFLAGS = -O3 -std=c++0x -UREF_COUNT -lpthread -UOPTIM_POS -DFIX_AMBIGUITY
%.o:%.cpp %.h
	$(CC) -c $(CFLAGS) $< -o $@
test%:test%.cpp pool_allocator.h
	$(CC) $(CFLAGS) $< -o $@ 
install:pool_allocator.h
	cp pool_allocator.h ifthenelse.hpp /usr/local/include/pool_allocator/
arm_install:pool_allocator.h
	cp pool_allocator.h ifthenelse.hpp /opt/ioplex_mx/usr/arm-buildroot-linux-gnueabihf/sysroot/usr/include/pool_allocator
