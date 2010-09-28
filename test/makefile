test.o: test.c
	gcc -c test.c

test.exe: test.o
	gcc -static -o test.exe test.o -L.. -lpthread

test: test.exe
	
clean:
	rm -rf *.o *.exe


