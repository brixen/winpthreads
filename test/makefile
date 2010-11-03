test.o: test.c
	gcc -c -m64 -I ../include -I ../src test.c

test.exe: test.o
	gcc -static  -m64 -o test.exe test.o -L.. -lpthread

mutex4.o: mutex4.c
	gcc -c -m64 -I ../include -I ../src mutex4.c

mutex4.exe: mutex4.o
	gcc -static  -m64 -o mutex4.exe mutex4.o -L.. -lpthread


test: test.exe
	
clean:
	rm -rf *.o *.exe
