##
# Temporary Makefile
#
# Treat warnings as errors. This seems to be the only way to
# convince some students of the importance of ensuring that
# their code compiles without warnings before starting to debug.
#
# Do not change this line.  We will not use your copy of the Makefile
# we will use *this* Makefile when grading.
#
# for benchmarking we'll use this.
# Note that this defines NDEBUG, which removes all assert()s from your code
CFLAGS=-pthread -Wall -O3 -Werror -Wmissing-prototypes -DNDEBUG
LDFLAGS=-pthread
LDLIBS=-lm
# for debugging, you may use these 2 instead
#LDFLAGS=-pthread -fsanitize=undefined
#CFLAGS=-Wall -O0 -ggdb -Werror -Wmissing-prototypes -fsanitize=undefined

OBJ=threadpool.o list.o threadpool_lib.o

ALL=quicksort psum_test fib_test mergesort nqueens simpson \
	threadpool_test threadpool_test2 threadpool_test3 threadpool_test4 threadpool_test5 \
	threadpool_test6 threadpool_test7 threadpool_test8 threadpool_test9

all: $(ALL)

threadpool_test9: threadpool_test9.o $(OBJ)

threadpool_test8: threadpool_test8.o $(OBJ)

threadpool_test7: threadpool_test7.o $(OBJ)

threadpool_test6: threadpool_test6.o $(OBJ)

threadpool_test5: threadpool_test5.o $(OBJ)

threadpool_test4: threadpool_test4.o $(OBJ)

threadpool_test3: threadpool_test3.o $(OBJ)

threadpool_test2: threadpool_test2.o $(OBJ)

threadpool_test: threadpool_test.o $(OBJ)

quicksort: quicksort.o $(OBJ)

nqueens: nqueens.o $(OBJ)

mergesort: mergesort.o $(OBJ)

psum_test: psum_test.o $(OBJ)

fib_test: fib_test.o $(OBJ)

simpson: simpson.o $(OBJ)

clean:
	rm -f *.o $(ALL)

