CFLAGS?=-std=gnu99 -O3 -Wall

default: multipart_parser.o

multipart_parser.o: multipart_parser.c multipart_parser.h

clean:
	rm -f *.o
	find -name '*.pyc' -delete
	find -name '__pycache__' -delete
	rm -rf build/
