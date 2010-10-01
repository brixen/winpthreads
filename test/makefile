test.o: test.c
	gcc -c -m64 test.c

test.exe: test.o
	gcc -static  -m64 -o test.exe test.o -L.. -lpthread

test: test.exe
	
clean:
	rm -rf *.o *.exe


