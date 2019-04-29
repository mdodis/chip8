@echo off

set libs=lib\SDL2.lib lib\SDL2main.lib

cl -nologo -FC -W2 -Fe:chip8.exe chip8.c -Iinclude -link %libs% /SUBSYSTEM:CONSOLE
