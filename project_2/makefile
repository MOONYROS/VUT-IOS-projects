all: proj2

proj2: proj2.o
	 gcc -o proj2 proj2.o -pthread

proj2.o: proj2.c
	 gcc -c -std=gnu99 -Wall -Wextra -Werror -pedantic proj2.c

clean:
	 rm proj2.o proj2
