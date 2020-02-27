all: chip8

chip8: chip8.c
	gcc -std=c89 -O3 -Wall chip8.c $(shell pkg-config --cflags --libs sdl2)  -lGL -lGLU -o chip8
