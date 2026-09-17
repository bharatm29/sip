main: main.c
	@ cc main.c -Wall -Wextra -ggdb -I./zlib/include/ -lraylib -lm -L./zlib/lib -lz -o main
	@ ./main
