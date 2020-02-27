/* (c) Michael Dodis, 2020
 * This code is licensed under the MIT license (see LICENSE.txt for details)
 * spec from: http://devernay.free.fr/hacks/chip8/C8TECH10.HTM
 *
 * TODO: batch ticks together, (timer will tell us how many ticks to run on every loop)
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <GL/gl.h>
#include <SDL2/SDL.h>

#define EMU_W 64u
#define EMU_H 32u
#define EMU_START 0x200

const uint8_t FONTSET[80] = {
0xF0,0x90,0x90,0x90,0xF0,0x20,0x60,0x20,0x20,0x70,0xF0,0x10,0xF0,0x80,0xF0,0xF0,
0x10,0xF0,0x10,0xF0,0x90,0x90,0xF0,0x10,0x10,0xF0,0x80,0xF0,0x10,0xF0,0xF0,0x80,
0xF0,0x90,0xF0,0xF0,0x10,0x20,0x40,0x40,0xF0,0x90,0xF0,0x90,0xF0,0xF0,0x90,0xF0,
0x10,0xF0,0xF0,0x90,0xF0,0x90,0x90,0xE0,0x90,0xE0,0x90,0xE0,0xF0,0x80,0x80,0x80,
0xF0,0xE0,0x90,0x90,0x90,0xE0,0xF0,0x80,0xF0,0x80,0xF0,0xF0,0x80,0xF0,0x80,0x80
};

static char MAP_NORMAL [16]= {
SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_8, SDL_SCANCODE_9,
SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_I, SDL_SCANCODE_O,
SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_K, SDL_SCANCODE_L,
SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD,
};

static struct {
    uint8_t   V[0x10];
    uint16_t  I;
    uint8_t   DT; /* delay timer 60hz -1 until 0 */
    uint8_t   ST; /* sound timer same but buzz until 0 */
    uint16_t  PC;
    int8_t   SP;
    uint8_t   DF; /* draw flag */
    uint8_t mem[0x1000];
    uint16_t stack[16];
    uint8_t keys[16];
    uint8_t disp[EMU_W * EMU_H];
} emu;

static struct {
    SDL_Window *wind;
    SDL_Renderer *rend;
    SDL_GLContext ctx;

    uint32_t cpu_simulation_freq;

    uint64_t cpu_counter;
    uint64_t timer_counter;
    uint64_t performance_frequency;
    const char *mappings;
} ui;

/* Prepares emulator for execution */
static void initialize(char *romfile);
/* Frees emulator resources */
static void cleanup();
/* Executes _one_ opcode */
static void cycle();


int main(int argc, char **argv) {

    if (argc <= 1) {
        fprintf(stderr,"USAGE: chip8 [ROM-FILE]\n");
        return 1;
    }

    initialize(argv[1]);

    ui.cpu_simulation_freq =600; /* Hz */

    ui.performance_frequency = SDL_GetPerformanceFrequency();
    ui.cpu_counter = ui.timer_counter = SDL_GetPerformanceCounter();

    emu.DF = 1;
    for (;;) {
        uint64_t time_now;
        SDL_Event e;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) goto _CLEANUP;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
               int ki;
               for (ki = 0; ki < 16; ++ki) {
                    if (e.key.keysym.scancode == ui.mappings[ki])
                        emu.keys[ki] = e.type == SDL_KEYDOWN;
               }
            }
        }

        time_now = SDL_GetPerformanceCounter();

        /* timers */
        {
            double cpu_diff = (double)(
                    (time_now - ui.cpu_counter) * 1000 /
                    ui.performance_frequency);
            double timer_diff = (double)(
                    (time_now - ui.timer_counter) * 1000 /
                    ui.performance_frequency);
            double cpu_target = (1000.0/((double)ui.cpu_simulation_freq));

            if ((1000.0/60.0) <= (timer_diff)) {
                if (emu.DT != 0) emu.DT--;
                if (emu.ST != 0) emu.ST--;

                ui.timer_counter = time_now;
            }

            if (cpu_target <= cpu_diff) {
                int tick_index;
                double tick_amount = cpu_diff / (double)cpu_target;

                for (tick_index = 0; tick_index < ((int)tick_amount); tick_index++)
                    cycle();

                ui.cpu_counter = time_now;
            } else {
                if (cpu_target - cpu_diff >= 0.9f) {
                    SDL_Delay(cpu_target - ((double)cpu_diff));
                }
            }
        }


        if (emu.DF) {
            int x, y;
            emu.DF = 0;
            glClear(GL_COLOR_BUFFER_BIT);

            for (y = 0; y < EMU_H; ++y) {
                for (x = 0; x < EMU_W; ++x) {
                    if (emu.disp[x + y * EMU_W])
                    {
                        glColor4f(1.f,1.f,1.f,1.f);
                        glRectf(x*10, y*10, x*10 + 10, y*10 + 10);
                    }
                }
            }
            SDL_GL_SwapWindow(ui.wind);
        }

    }
_CLEANUP:
    cleanup();
    return 0;
}

void initialize(char *romfile) {
    FILE *f; size_t size;
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    ui.wind = SDL_CreateWindow(
            "CHIP-8",
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            EMU_W * 10, EMU_H * 10,
            SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    assert(ui.wind);
    ui.ctx = SDL_GL_CreateContext(ui.wind);
    assert(ui.ctx);

    ui.mappings = MAP_NORMAL;

    glClearColor(0.f,0.f,0.f,1.f);
    glViewport(0,0,EMU_W * 10, EMU_H * 10);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glOrtho(0, EMU_W * 10, EMU_H * 10, 0, 10, -10);

    /* init important memory areas */
    memcpy(emu.mem, FONTSET, sizeof(FONTSET));
    emu.PC = EMU_START;

    f = fopen(romfile, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* TODO: check file size! */
    fread(emu.mem + EMU_START, size, 1, f);
    fclose(f);

    srand(time(0));
}

void cleanup() {
    SDL_Quit();
    puts("Bye!");
    exit(0);
}

#define PCINC() emu.PC+=2
#define INSTR \
I("00E0 - CLS"          , op == 0x00E0, memset(emu.disp, 0, EMU_W*EMU_H); emu.DF = 1; PCINC();)\
I("00EE - RET"          , op == 0x00EE, i_ret();)\
I("0nnn - SYS"          , u == 0, PCINC();)\
I("1nnn - JP addr"      , u == 1, emu.PC = nnn;)\
I("2nnn - CALL addr"    , u == 2, i_call(nnn);)\
I("3xkk - SE Vx, byte"  , u == 3, emu.PC += (emu.V[x]==kk) ? 4 : 2;)\
I("4xkk - SNE Vx, byte" , u == 4, emu.PC += (emu.V[x]!=kk) ? 4 : 2;)\
I("5xy0 - SE Vx, Vy"    , u == 5, emu.PC += (emu.V[x]==emu.V[y]) ? 4 : 2;)\
I("6xkk - LD Vx, byte"  , u == 6, emu.V[x]  = kk; PCINC();)\
I("7xkk - ADD Vx, byte" , u == 7, emu.V[x] += kk; PCINC();)\
I("8xy0 - LD Vx, Vy"    , u == 8 && l == 0, emu.V[x]  = emu.V[y]; PCINC();)\
I("8xy1 - OR Vx, Vy"    , u == 8 && l == 1, emu.V[x] |= emu.V[y]; PCINC();)\
I("8xy2 - AND Vx, Vy"   , u == 8 && l == 2, emu.V[x] &= emu.V[y]; PCINC();)\
I("8xy3 - XOR Vx, Vy"   , u == 8 && l == 3, emu.V[x] ^= emu.V[y]; PCINC();)\
I("8xy4 - ADD Vx, Vy"   , u == 8 && l == 4, emu.V[15]=(emu.V[x] > 0xFF - emu.V[y]); emu.V[x] += emu.V[y]; PCINC();)\
I("8xy5 - SUB Vx, Vy"   , u == 8 && l == 5, emu.V[15]=(emu.V[x] > emu.V[y]); emu.V[x] -= emu.V[y]; PCINC();)\
I("8xy6 - SHR Vx, Vy"   , u == 8 && l == 6, emu.V[15]=(emu.V[x] & 1); emu.V[x] /= 2; PCINC();)\
I("8xy7 - SUBN Vx, Vy"  , u == 8 && l == 7, emu.V[15]=(emu.V[y] > emu.V[x]); emu.V[y] -= emu.V[x]; PCINC();)\
I("8xyE - SHL Vx, Vy"   , u == 8 && l == 14,emu.V[15]=(emu.V[x] >> 7); emu.V[x] *= 2; PCINC();)\
I("9xy0 - SNE Vx, Vy"   , u == 9 , emu.PC += (emu.V[x]!=emu.V[y]) ? 4 : 2;)\
I("Annn - LD I, addr"   , u == 10, emu.I = nnn; PCINC();)\
I("Bnnn - JP V0, addr"  , u == 11, emu.PC = nnn + emu.V[0];)\
I("Cxkk - RND Vx, byte" , u == 12, emu.V[x] = (rand()%256) & kk; PCINC();)\
I("Dxyn - DRW Vx, Vy, nibble", u == 13, i_draw(emu.V[x], emu.V[y], n); PCINC();)\
I("Ex9E - SKP Vx"       , u == 14 && kk == 0x9E, emu.PC += ( emu.keys[emu.V[x]]) ? 4 : 2;)\
I("ExA1 - SKNP Vx"      , u == 14 && kk == 0xA1, emu.PC += (!emu.keys[emu.V[x]]) ? 4 : 2;)\
I("Fx07 - LD Vx, DT"    , u == 15 && kk == 0x07, emu.V[x] = emu.DT; PCINC();)\
I("Fx0A - LD Vx, K"     , u == 15 && kk == 0x0A, emu.V[x] = i_waitkey(); PCINC();)\
I("Fx15 - LD DT, Vx"    , u == 15 && kk == 0x15, emu.DT = emu.V[x]; PCINC();)\
I("Fx18 - LD ST, Vx"    , u == 15 && kk == 0x18, emu.ST = emu.V[x]; PCINC();)\
I("Fx1E - ADD I, Vx"    , u == 15 && kk == 0x1E, emu.V[15] =((int)emu.I + (int)emu.V[x] > (int)0xfff); emu.I += emu.V[x]; PCINC();)\
I("Fx29 - LD F, Vx"     , u == 15 && kk == 0x29, emu.I = 5 * emu.V[x]; PCINC();)\
I("Fx33 - LD B, Vx"     , u == 15 && kk == 0x33, i_bcd(emu.I, emu.V[x]); PCINC();)\
I("Fx55 - LD [I], Vx"   , u == 15 && kk == 0x55, memcpy(emu.mem + emu.I, emu.V, x+1); PCINC();)\
I("Fx65 - LD Vx, [I]"   , u == 15 && kk == 0x65, memcpy(emu.V, emu.mem + emu.I, x+1); PCINC();)\

static void i_call(uint16_t addr) {
    /* assert((emu.SP + 1) < sizeof(emu.stack)); */
    emu.stack[emu.SP++] = emu.PC + 2;
    emu.PC = addr;
}

static void i_ret() {
    /* assert(emu.SP > 0); */
    emu.PC = emu.stack[--emu.SP];
}

static void i_draw(uint8_t x,uint8_t y,uint8_t n) {
    int j;
    emu.V[15] = 0;
    emu.DF = 1;
    for (j = 0; j < n; ++j) {
        int i;
        uint8_t pb = emu.mem[emu.I + j];
        for (i = 0; i < 8; ++i) {
            uint8_t px = pb & (0x80 >> i);
            int pos = (x+i)%EMU_W + ((y+j)%EMU_H)*EMU_W;
            if (!(px && emu.disp[pos]))
                emu.V[15] = 1;

            emu.disp[pos] ^= px;
        }
    }
}

uint8_t i_waitkey() {
    SDL_Event e;

    while(SDL_WaitEvent(&e)) {
        int i;
        if (e.type == SDL_QUIT) exit(0);
        if (e.type != SDL_KEYUP) continue;

        for (i = 0; i < 16; ++i){
            if (e.key.keysym.scancode == ui.mappings[i])
                return i;
        }
    }

    return -1;
}

static void i_bcd(uint16_t I, uint8_t vx) {
    emu.mem[I + 0] = (vx%1000)/100;
    emu.mem[I + 1] = (vx%100)/10;
    emu.mem[I + 2] = (vx%10);
    return;
}

void cycle() {
    uint16_t op = emu.mem[emu.PC]<<8 | emu.mem[emu.PC+1];
    uint16_t nnn = op & 0x0FFF;
    uint8_t n = op & 0x000F;
    uint8_t x = (op & 0x0F00) >> 8;
    uint8_t y = (op & 0x00F0) >> 4;
    uint8_t kk = op & 0x00FF;
    uint8_t u = op >> 12;
    uint8_t l = (op & 0x000F);

#define I(str, cond, ex) if ((cond)) { ex return;}
    INSTR
#undef I

    cleanup();
#undef PCINC
}
