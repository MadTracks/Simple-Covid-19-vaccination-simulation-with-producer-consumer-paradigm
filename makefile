target:	program

program: program.c
		gcc program.c -o program -lpthread -lrt -Wall
clean:
		rm program