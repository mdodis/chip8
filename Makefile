
all: chip8.c
	gcc -O3 -o chip8 $(shell pkg-config --cflags --libs sdl2) -lGL chip8.c

clean:
	@rm chip8
