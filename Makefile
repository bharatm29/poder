main: main.c
	@ cc main.c -ggdb -Iraylib -I./zlib/include/ -lraylib -lm -L./zlib/lib -lz -o main
	@ ./main
