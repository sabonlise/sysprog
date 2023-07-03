GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant -fstack-protector-all -g

all: solution.c
	gcc $(GCC_FLAGS) solution.c ../utils/heap_help/heap_help.c -ldl -rdynamic

clean:
	rm a.out
