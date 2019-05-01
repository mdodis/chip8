@echo off

set libs=lib\SDL2.lib lib\SDL2main.lib opengl32.lib

cl -nologo -FC -DPLATWIND -Fe:chip8.exe chip8.c -Iinclude -link %libs% /SUBSYSTEM:CONSOLE
