#include <GL/gl.h>
#include <SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

SDL_Window*     gwindow;
SDL_Renderer*   grender;

#define EMU_W 64
#define EMU_H 32
#define SCALE 10


#define START     0x200
#define MODE_STEP 0

#ifdef NOPRINT
#define dprint(...)
#endif

struct
{
    unsigned char   v[16];
    uint16_t        I;
    uint16_t        pc;
    uint8_t         sp;
}rf;

unsigned char   mem[4096];
uint16_t        stack[16];
unsigned char   keys[16];
unsigned char   disp[EMU_W * EMU_H];
unsigned char   tm_delay;
unsigned char   tm_sound;
unsigned char   draw_flag = 0;

unsigned char fontset[80] =
{
  0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
  0x20, 0x60, 0x20, 0x20, 0x70, // 1
  0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
  0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
  0x90, 0x90, 0xF0, 0x10, 0x10, // 4
  0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
  0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
  0xF0, 0x10, 0x20, 0x40, 0x40, // 7
  0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
  0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
  0xF0, 0x90, 0xF0, 0x90, 0x90, // A
  0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
  0xF0, 0x80, 0x80, 0x80, 0xF0, // C
  0xE0, 0x90, 0x90, 0x90, 0xE0, // D
  0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
  0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

static void c8_init();
static void c8_cycle();

static void c8_listreg()
{
    // print registers
    puts("REGISTERS========*");
    for (char i = 0; i < 16; ++i)
    {
        //printf("[%01x]=<%02x>", i, rf.v[i]);
        //if (i % 2 == 0) printf(" ");
        //else printf("|\n");
    }
    //printf("I=<%04x>         |\n", rf.I);
    //printf("PC=<%04x>        |\n", rf.pc);
    //printf("SP=<%02x>          |\n", rf.sp);
    puts("=================*");
}

static void c8_listdisp()
{
    for (int j = 0; j < EMU_H; ++j)
    {
        for (int i = 0; i < EMU_W; ++i)
        {
            if (!disp[j + i * EMU_H])
                printf("0");
            else
                printf("1");
        }
        printf("\n");
    }
}

static void c8_liststack()
{
    for (int i = 0; i <= rf.sp; ++i)
    {
        printf("%d: [%04x] \n",i,stack[i]);
    }
}

static void c8_listmemI()
{
    const int amount = 64;

    for (int i = 0; i < amount; ++i)
    {
        if ((unsigned char)mem[rf.I + i] == 1)
            printf("1");
        else
            printf("0");
    }
    printf("\n");
}

/*

1 2 3 C === 1 2 3 4
4 5 6 D === q w e r
7 8 9 E === a s d f
A 0 B F === z x c v

*/

static char km_keyboard[16] =
{
    SDL_SCANCODE_X,
    SDL_SCANCODE_1,SDL_SCANCODE_2,SDL_SCANCODE_3,
    SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E,
    SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D,

    SDL_SCANCODE_Z, SDL_SCANCODE_C,

    SDL_SCANCODE_4, SDL_SCANCODE_R, SDL_SCANCODE_F,
    SDL_SCANCODE_V
};

static char km_numpad [16] =
{
    SDL_SCANCODE_KP_0,
    SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3,
    SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6,
    SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9,
    SDL_SCANCODE_KP_PERIOD, SDL_SCANCODE_KP_ENTER,
    SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_KP_MINUS,
    SDL_SCANCODE_KP_MULTIPLY, SDL_SCANCODE_KP_DIVIDE
};

static char keymap[16];
static char hexpad[16] =
{
    0, 0, 0,
    0, 0, 0,
    0, 0, 0,
    0, 0, 0,
    0, 0, 0,
    0,
};

static unsigned char c8_waitkey()
{
    SDL_Event e;

    while(SDL_WaitEvent(&e))
    {

        if (e.type == SDL_QUIT) exit(0);
        if (e.type != SDL_KEYUP) continue;

        for (unsigned char i = 0; i < 16; ++i)
        {
            if (e.key.keysym.sym == keymap[i])
            {
                return i;
            }
        }
    }

    return -1;
}

static void c8_getkeys()
{
    const Uint8* keystate = SDL_GetKeyboardState(NULL);
    for (unsigned char i = 0; i < 16; ++i)
    {
        hexpad[i] = keystate[keymap[i]];
    }

}

static unsigned char c8_rand()
{
   return (unsigned char)(rand() % 256);
}

int main(int argc, char* args[])
{
    srand(time(0));

    SDL_Event e;


    if (argc <= 1)
    {
        //printf("Usage: chip8  <Rom File> [NONUMPAD]\n");

        return -1;
    }

    if ((argc > 2) && !strcmp("-NONUMPAD", args[2]))
    {
        for (int i = 0; i < 16; ++i)
        {
            keymap[i] = km_keyboard[i];
        }
    }
    else
    {
        for (int i = 0; i < 16; ++i)
        {
            keymap[i] = km_numpad[i];
        }
    }

    c8_init(args[1]);


    if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
        exit(-1);

    gwindow = SDL_CreateWindow("chip8",
        100,
        100,
        EMU_W * SCALE,
        EMU_H * SCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);

    SDL_GLContext ctx = SDL_GL_CreateContext(gwindow);
    glClearColor(0.f,0.f,0.f,1.f);
    assert(gwindow);

    glViewport(0,0,EMU_W * SCALE, EMU_H * SCALE);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glOrtho(0, EMU_W * SCALE, EMU_H * SCALE, 0, 10, -10);



    Uint32 tm = SDL_GetTicks();
    while (1)
    {
    #if 1
        while(SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)exit(0);
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
            {
                for (int i = 0; i < 16; ++i)
                {
                    if (e.key.keysym.scancode == keymap[i])
                    {
                        hexpad[i] = e.type == SDL_KEYDOWN;
                    }
                }
            }

        }

        if (SDL_GetTicks() - tm >= 10)
        {
            c8_cycle();
            c8_cycle();
            c8_cycle();
            tm = SDL_GetTicks();


            if (draw_flag)
            {

                glClear(GL_COLOR_BUFFER_BIT);
                draw_flag = 0;
                for (int j = 0; j < EMU_H; ++j)
                {
                    for (int i = 0; i < EMU_W; ++i)
                    {
                        float rx = i * SCALE;
                        float ry = j * SCALE;
                        float rw = SCALE;
                        float rh = SCALE;
                        if (disp[i + j*EMU_W])
                        {
                            glColor4f(1.0f,1.f,1.f, 1.0f);
                            glRectf(rx, ry, rx + rw, ry + rh);
                        }
                    }
                }
            }

        }
        #else

        c8_cycle();

        if (draw_flag)
        {

            glClear(GL_COLOR_BUFFER_BIT);
            draw_flag = 0;
            for (int j = 0; j < EMU_H; ++j)
            {
                for (int i = 0; i < EMU_W; ++i)
                {
                    float rx = i * SCALE;
                    float ry = j * SCALE;
                    float rw = SCALE;
                    float rh = SCALE;
                    if (disp[i + j*EMU_W])
                    {
                        glColor4f(1.0f,1.f,1.f, 1.0f);
                        glRectf(rx, ry, rx + rw, ry + rh);
                    }
                }
            }
        }
        while(SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)exit(0);
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
            {
                for (int i = 0; i < 16; ++i)
                {
                    if (e.key.keysym.scancode == keymap[i])
                    {
                        hexpad[i] = e.type == SDL_KEYDOWN;
                    }
                }
            }

        }
        #endif

        SDL_GL_SwapWindow(gwindow);
    }

    return 0;
}

static void c8_cycle()
{
    uint16_t op = mem[rf.pc] << 8 | mem[rf.pc + 1];
    //// printf("op: <%04x>\n",op);


    uint16_t imm3 = (op & 0x0FFF);
    uint8_t  imm2 = (op & 0x00FF);
    uint8_t  imm1 = (op & 0x000F);
    uint8_t  vx   = (op & 0x0F00) >> 8;
    uint8_t  vy   = (op & 0x00F0) >> 4;

    switch(op & 0xF000)
    {
        case 0x0000:
        {
            if (op == 0x00E0)   // 00E0 - CLS
            {
                memset(disp, 0, EMU_W*EMU_H);

                draw_flag = 1;
                //printf("cls\n");
                rf.pc +=2;
            }
            else if (op == 0x00EE)  // 00EE - RET
            {
                //printf("ret\n");
                rf.pc = stack[--rf.sp];
            }
            else
            {
                //printf("INVALID OP, EXIT");
                exit(0);
            }
        } break;

        case 0x1000:    // 1nnn - JP addr
        {
            //printf("jp <%04x>\n",imm3);
            rf.pc = imm3;
        } break;

        case 0x2000:    // 2nnn - CALL addr
        {
            //printf("call <%03x>\n", imm3);
            stack[rf.sp++] = rf.pc + 2;
            rf.pc = imm3;
        } break;


        case 0x3000:    // 3xkk - SE Vx, byte
        {
            //printf("se v%01x, %02x\n", vx, imm2);
            rf.pc += (rf.v[vx] == imm2) ? 4 : 2;

            //if (rf.v[vx] != imm2)
                //printf("SKIP FAILED\t (%04x) != %02x \n", rf.v[vx], imm2);
            //else
                //printf("SKIP successful\t (%04x) == %02x \n", rf.v[vx], imm2);
        } break;


        case 0x4000:    // 4xkk - SNE Vx, byte
        {
            //printf("sne v%01x, %02x\n", vx, imm2);
            rf.pc += (rf.v[vx] != imm2) ? 4 : 2;
        } break;

        case 0x5000:    // 5xy0 - SE Vx, Vy
        {
            // printf("se v%01x, v%01x\n",vx,vy);

            if (rf.v[vx] == rf.v[vy])
            {
                //printf("\tSKIP successful\n");
                rf.pc += 2;
            }
            rf.pc += 2;
        } break;

        case 0x6000:    // 6xkk - LD Vx, byte
        {

            //printf("ld v%01x, %02x\n", vx, imm2);

            rf.v[vx] = imm2;

            rf.pc+=2;
        } break;

        case 0x7000:    // 7xkk - ADD Vx, byte
        {
            rf.v[vx] += imm2;
            //printf("add v%01x, %02x\n",vx, imm2);

            rf.pc += 2;
        } break;

        case 0x8000:
        {
            switch(imm1)
            {
                case 0x0:   // 8xy0 - LD Vx, Vy
                {
                    //printf("ld v%01x, v%01x\n", vx, vy);
                    rf.v[vx] = rf.v[vy];

                    rf.pc += 2;
                } break;

                case 0x1:   // 8xy1 - OR Vx, Vy
                {
                    //printf("or v%01x, v%01x\n", vx, vy);
                    rf.v[vx] = rf.v[vx] | rf.v[vy];

                    rf.pc += 2;
                } break;

                case 0x2:   // 8xy2 - AND Vx, Vy
                {
                    //printf("and v%01x, v%01x\n", vx, vy);
                    rf.v[vx] = rf.v[vx] & rf.v[vy];

                    rf.pc += 2;
                } break;

                case 0x3:   // 8xy3 - XOR Vx, Vy
                {
                    //printf("xor v%01x, v%01x\n", vx, vy);
                    rf.v[vx] = rf.v[vx] ^ rf.v[vy];

                    rf.pc += 2;
                } break;

                case 0x4:   // 8xy4 - ADD Vx, Vy
                {
                    //printf("add v%01x, v%01x\n", vx, vy);

                    if ((int)rf.v[vx] + (int) rf.v[vy] > 255) rf.v[15] = 1;
                    else rf.v[15] = 0;

                    rf.v[vx] = rf.v[vx] + rf.v[vy];
                    rf.pc += 2;
                } break;


                case 0x5:   // 8xy5 - SUB Vx, Vy
                {
                    //printf("sub v%01x, v%01x\n", vx, vy);

                    rf.v[15] = rf.v[vx] > rf.v[vy];
                    rf.v[vx] = rf.v[vx] - rf.v[vy];
                    rf.pc += 2;
                } break;

                case 0x6:   // 8xy6 - SHR Vx {, Vy}
                {
                    rf.v[0xF] = 0;
                    if (rf.v[vx] & (1)) rf.v[0xF] = 1;

                    rf.v[vx] = rf.v[vx] >> 1;
                    //printf("shr v%01x\n",vx);
                    rf.pc += 2;
                } break;

                case 0x7:   // 8xy7 - SUBN Vx, Vy
                {
                    //printf("subn v%01x, v%01x\n",vx, vy);

                    rf.v[15] = rf.v[vy] > rf.v[vx];
                    rf.v[vx] = rf.v[vy] - rf.v[vx];
                    rf.pc += 2;
                } break;

                case 0xE:   // 8xyE - SHL Vx {, Vy}
                {
                    rf.v[0xF] = 0;
                    if (rf.v[vx] & (1 << 7)) rf.v[0xF] = 1;
                    rf.v[vx] = rf.v[vx] << 1;

                    //printf("shl v%01x\n",vx);

                    rf.pc += 2;
                } break;

                default:
                {
                    //printf("%04x unknown!\n",op);
                    exit(-2);
                } break;
            }
        } break;

        case 0x9000:    // 9xy0 - SNE Vx, Vy
        {
            //printf("sne v%01x, v%01x", vx, vy);
            rf.pc += (rf.v[vx] != rf.v[vy]) ? 4 : 2;

        } break;

        case 0xA000:    // Annn - LD I, addr
        {
            rf.I = imm3;
            //printf("ld I, %04x\n", imm3);
            rf.pc+=2;
        } break;

        case 0xB000:    // Bnnn - JP V0, addr
        {
            //printf("jp v0, %04x\n", imm3);

            rf.pc = imm3 + rf.v[0];
        } break;

        case 0xC000:    // Cxkk - RND Vx, byte
        {
            rf.v[vx] = c8_rand() & imm2;
            //printf("rnd V%01x, %02x\n", vx, imm2);
            rf.pc+=2;
        } break;

        case 0xD000:    // Dxyn - DRW Vx, Vy, nibble
        {
            unsigned char x = rf.v[vx];
            unsigned char y = rf.v[vy];

            int i = 0;

            rf.v[15] = 0;
            /*printf("drw v%01x(%02x),v%01x(%02x) %d from I(%04x)\n",
                vx,x,
                vy,y,
                imm1,
                rf.I);*/

            draw_flag = 1;
            for (int j = 0; j < imm1; ++j)
            {
                uint16_t pixel = mem[rf.I + j];
                for (int i = 0; i < 8; ++i)
                {
                    if ((pixel & (0x80 >> i)) != 0)
                    {
                        if (disp[x + i + (y + j) * EMU_W] == 1)
                            rf.v[15] = 1;
                        disp[x + i + (y + j) * EMU_W] ^= 1;
                    }
                }
            }

            rf.pc += 2;
        } break;

        case 0xE000:
        {
            unsigned char fn = op & 0x00FF;
            if (imm2 == 0xA1) // ExA1 - SKNP Vx
            {
                unsigned char rs = (op & 0x0F00) >> 8;
                rf.pc += hexpad[rf.v[vx]] != 1 ? 4 : 2;
                //printf("skp v%01x\n", vx);
            }
            else if (imm2  == 0x9E) // Ex9E - SKP Vx
            {
                unsigned char rs = (op & 0x0F00) >> 8;
                rf.pc += hexpad[rf.v[vx]] == 1 ? 4 : 2;
                //printf("skpn v%01x\n", vx);
            }
            else exit(0);

        } break;

        case 0xF000:
        {
            switch (imm2)
            {
                case 0x07:  // Fx07 - LD Vx, DT
                {
                    rf.v[vx] = tm_delay;
                    //printf("ld v%01x, DT\n", vx);
                    rf.pc += 2;
                } break;

                case 0x0A:  // Fx0A - LD Vx, K
                {
                    unsigned char key = c8_waitkey();
                    rf.v[vx] = key;
                    //printf("ld v%01x, K(%01x)\n",vx, key);
                    rf.pc += 2;
                } break;

                case 0x15:  // Fx15 - LD DT, Vx
                {
                    //printf("ld dt, v%01x\n",vx );
                    tm_delay = rf.v[vx];
                    rf.pc += 2;
                } break;

                case 0x18:  // Fx18 - LD ST, Vx
                {
                    //printf("ld ST, v%01x\n", vx);
                    tm_sound = rf.v[vx];
                    rf.pc += 2;
                } break;

                case 0x1E:  // Fx1E - ADD I, Vx
                {
                    // unsigned char rs = (op & 0x0F00) >> 8;

                    //printf("add I, v%01x\n", vx);
                    rf.v[15] = ((int)rf.I + (int)rf.v[vx] > (int)0xfff);
                    rf.I += rf.v[vx];
                    rf.pc += 2;
                } break;

                case 0x29:  // Fx29 - LD F, Vx
                {
                    unsigned char rs = (op & 0x0F00) >> 8;
                    /*
                    Set I = location of sprite for digit Vx.
                    The value of I is set to the location
                    for the hexadecimal sprite corresponding to the
                    value of Vx.See section 2.4, Display,
                    for more information on the Chip-8 hexadecimal font.
                    */
                    //printf("SET I to digit: %01x\n", vx);
                    rf.I = 5 * rf.v[vx];

                    rf.pc += 2;
                } break;

                case 0x33:  // Fx33 - LD B, Vx
                {
                    unsigned char rs = (op & 0x0F00) >> 8;
                    //printf("store BCD of <%01x> to I0-I2\n", vx);

                    mem[rf.I + 2] = rf.v[vx] % 10;
                    mem[rf.I + 1] = (rf.v[vx] % 100) / 10;
                    mem[rf.I + 0] = (rf.v[vx] % 1000) / 100;

                    rf.pc += 2;
                } break;

                case 0x55:  // Fx55 - LD [I], Vx
                {

                    for (int i = 0; i <= vx; ++i)
                        mem[rf.I + i] = rf.v[i];

                    //printf("ld I, v%01x\n", vx);
                    rf.pc += 2;
                } break;


                case 0x65:  // Fx65 - LD Vx, [I]
                {
                    unsigned char rs = (op & 0x0F00) >> 8;

                    //printf("Read I from v0 to v%01x\n", vx);

                    for (unsigned char i = 0; i <= vx; ++i)
                        rf.v[i] = mem[rf.I + i];

                    rf.pc += 2;
                } break;

                default:
                {
                    //printf("%04x unknown!\n",op);
                    exit(-2);
                }break;
            }
        } break;

        default:
        {
            //printf("%04x unknown!\n",op);
            exit(-2);
        }break;
    }

    if (tm_delay > 0)
        tm_delay--;

    if (tm_sound > 0)
    {
        tm_sound--;
        if (!tm_sound) puts("BEEP");
    }
}

static void c8_init(char* program)
{
    size_t program_size;

    rf.pc = START;
    rf.I = 0;
    rf.sp = 0;
    tm_delay = 0;
    tm_sound = 0;

    memset(disp, 0, EMU_W*EMU_H);


    for (int i = 0; i < 80; ++i)
    {
        mem[i] = fontset[i];
    }

    // load program
    FILE* f = fopen(program, "rb");
    fseek(f, 0, SEEK_END);
    program_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    fread(mem + START, program_size, 1, f);
    fclose(f);

}

static void c8_tick()
{

}
