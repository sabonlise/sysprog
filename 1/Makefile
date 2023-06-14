GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant

all: libcoro.c solution.c
	gcc $(GCC_FLAGS) libcoro.c solution.c ../utils/heap_help/heap_help.c -ldl -rdynamic

clean:
	rm a.out
