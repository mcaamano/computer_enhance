all:  page_faults1

CC			=	gcc
CFLAGS		=	-I. -I../rdtsc -I../rep_tester -std=c11 -g -Wall -Werror -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LD_FLAGS	= 	-L. -L../rdtsc -L../rep_tester
DEPS 		=	

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@ 

page_faults1: page_faults1.o 
	$(CC) $(LD_FLAGS) $@.o -o $@ -lreptester -lrdtsc_utils


.PHONY: clean

clean:
	rm -f *.o *.a a.out page_faults1
