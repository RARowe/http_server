main:
	gcc -ansi -lsqlite3 main.c

clean:
	rm a.out

run:
	./a.out
