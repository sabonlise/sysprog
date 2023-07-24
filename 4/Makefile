GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant -pthread -g

all: test.o thread_pool.o
	gcc $(GCC_FLAGS) test.o thread_pool.o heap_help.o -ldl -rdynamic

test.o: test.c
	gcc $(GCC_FLAGS) -c test.c -o test.o -I ../utils

thread_pool.o: thread_pool.c
	gcc $(GCC_FLAGS) -c thread_pool.c -o thread_pool.o

heap_help.o: ../utils/heap_help/heap_help.c
	gcc $(GCC_FLAGS) -c ../utils/heap_help/heap_help.c -o heap_help.o
