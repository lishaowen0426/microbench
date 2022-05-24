CC=gcc  #If you use GCC, add -fno-strict-aliasing to the CFLAGS because the Google BTree does weird stuff
CFLAGS=-Wall -O0 -ggdb3 -I/home/shaowen/pmdk/install/include
#CFLAGS=-O2 -ggdb3 -Wall
#CFLAGS=-O2 -ggdb3 -Wall  -fno-omit-frame-pointer

CXX=clang++
CXXFLAGS= ${CFLAGS} -std=c++11

LDLIBS=-lm -lpthread -lstdc++ -L/home/shaowen/pmdk/install/lib -lpmem -lnuma
LDFLAGS=-Wl,-rpath,/home/shaowen/pmdk/install/lib

MICROBENCH_OBJ=microbench.o

.PHONY: all clean

all: makefile.dep microbench

makefile.dep: *.[Cch]
	for i in *.[Cc]; do ${CC} -MM "$${i}" ${CFLAGS} ${LDLIBS}; done > $@

-include makefile.dep

microbench: $(MICROBENCH_OBJ)

clean:
	rm -f *.o microbench

