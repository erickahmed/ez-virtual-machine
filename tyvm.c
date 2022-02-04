/*
    TVM is a virtual machine for LC-3 based operating systems and it is used for educational purposes.
    Copyright (c) under MIT license
    Written by Erick Ahmed, 2022
*/

//#define __UNIX

/* universal libraries */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

/* unix only libraries */
// FIXME: solve include errors
#ifdef __UNIX
    #include <unistd.h>
    #include <fcntl.h>

    /* sys libraries */
    #include <sys/time.h>
    #include <sys/types.h>
    #include <termios.h>
    #include <mman.h>
#endif

#define TRUE 1
#define FALSE 0


/* Initializing 10 registers of which:
8 general purpose, 1 program counter, 1 conditional */
enum registers {
    RG_000 = 0,
    RG_001,
    RG_010,
    RG_011,
    RG_100,
    RG_101,
    RG_110,
    RG_111,
    RG_PC,           // program counter
    RG_COND,         // condition flag
    RG_COUNT
};

/* Initializing memory and register storages */
uint16_t memory[UINT16_MAX];
uint16_t reg[RG_COUNT];

/* Creating instruction set opcodes */
enum opcodes {      // [name, 8-bit value]
    OP_BR = 0,      // branch, 0000
    OP_ADD,         // add, 0001
    OP_LD,          // load, 0010
    OP_ST,          // store, 0011
    OP_JSR,         // jump register, 0100
    OP_AND,         // bitwise and, 0101
    OP_LDR,         // load register, 0110
    OP_STR,         // store register, 0111
    OP_RTI,         // unused opcode
    OP_NOT,         // bitwise not, 1001
    OP_LDI,         // indirect load, 1010
    OP_STI,         // indirect store, 1011
    OP_JMP,         // jump, 1100
    OP_RES,         // reserved opcode,
    OP_LEA,         // load effective address, 1110
    OP_TRAP,        // execute trap,
};

/* Creating condition flags */
enum flags {
    FL_P = 1,         // Positive
    FL_Z = 1 << 1,    // Zero
    FL_N = 1 << 2,    // Negative
};

/* Sign extension function for immediate add mode (imm5[0:4])
transforms 5bit number to 8bit number preserving sign*/
uint16_t sign_extend(uint16_t n, int bit_count) {
    if((n >> (bit_count - 1)) & 1) {
        n |= (0xFFFF << bit_count);
    }
    return n;
}

/* Flag update function
Every time a value is written to a register the flag will be updated */
void update_flags(uint16_t r) {
    if (reg[r] == 0) reg[RG_COND] = FL_Z;
    else if (reg[r] >> 15) reg[RG_COND] = FL_N;
    else reg[RG_COND] = FL_P;
}

/* main loop */
int main(int argc, const char* argv[]) {

    if(argc < 2) {
        printf("usage: [image-file1] ...\n");
        exit(2);
    }

    for(int i = 1; i < argc; i++) {
        printf("failed to load image: %s\n", argv[i]);
        exit(1);
    }

    signal(SIGINT, handle_interrupt());     //FIXME: handle_interrupt may not be correct, gives an error without semicolons (should be without)
    disable_input_buffering();

    reg[RG_COND] = FL_Z;
    reg[RG_PC]   = 0x3000;     //0x3000 is default load address

    int running = TRUE;
    while(running) {
        uint16_t instr = mem_read(reg[RG_PC]++);
        uint16_t op    = instr >> 12;

        switch (op) {
            case OP_BR:
                uint16_t cond      = (instr >> 9) & 0x7;
                uint16_t PCoffset9 = sign_extend(instr & 0x1FF, 9);

                if(cond & reg[RG_COND]) reg[RG_PC] += PCoffset9;

                break;
            case OP_ADD:
                uint16_t dr       = (instr >> 9) & 0x7;  // destination register
                uint16_t sr1      = (instr >> 6) & 0x7;  // source register 1
                uint16_t sr2;                            // source register 2
                uint16_t imm_flag = (instr >> 5) & 0x1;  // immediate mode flag (bit[5])
                uint16_t imm5;                           // immediate mode register

                if(imm_flag == 0) {
                    sr2 = (instr & 0x7);
                    reg[dr] = reg[sr1] + sr2;            // register mode add
                } else {
                    imm5 = sign_extend(instr & 0x1F, 5);
                    reg[dr] = reg[sr1] + imm5;           // immediate mode add
                }

                update_flags(dr);

                break;
            case OP_LD:
                uint16_t dr        = (instr >> 9) & 0x7;
                uint16_t PCoffset9 = sign_extend(instr & 0x1FF, 9);     // 9-bit value that indicates where to load the address when added to RG_PC

                reg[dr] = mem_read(PCoffset9 + reg[RG_PC]);

                update_flags(dr);

                break;
            case OP_ST:
                uint16_t sr        = (instr >> 6) & 0x7;
                uint16_t PCoffset9 = sign_extend(instr & 0x1FF, 9);     // 9-bit value that indicates where to load the address when added to RG_PC

                reg[sr] = mem_read(PCoffset9 + reg[RG_PC]);

                update_flags(dr);

                break;
            case OP_JSR:
                uint16_t PCoffset11  = sign_extend(instr & 0x1FF, 11);
                uint16_t jsr_flag    = (instr >> 11) & 0x1;
                uint16_t BaseR_jsrr  = (instr >> 6) & 0x7;          // JSRR only ecoding

                if(jsr_flag == 0) reg[RG_PC] = BaseR_jsrr;          // JSRR
                else reg[RG_PC] += PCoffset11;                      // JSR

                break;
            case OP_AND:
                uint16_t dr       = (instr >> 9) & 0x7;
                uint16_t sr1      = (instr >> 6) & 0x7;
                uint16_t sr2;
                uint16_t imm_flag = (instr >> 5) & 0x1;
                uint16_t imm5;

                if(imm_flag == 0) {
                    sr2 = (instr & 0x7);
                    reg[dr] = reg[sr1] & sr2;            // register mode and
                } else {
                    imm5 = sign_extend(instr & 0x1F, 5);
                    reg[dr] = reg[sr1] & imm5;           // immediate mode and
                }

                update_flags(dr);

                break;
            case OP_LDR:
                uint16_t dr      = (instr >> 9) & 0x7;
                uint16_t BaseR   = (instr >> 6) & 0x7;
                uint16_t offset6 = sign_extend(instr & 0x3FF, 6);

                reg[dr] = mem_read(reg[BaseR] + offset6);

                update_flags(dr);

                break;
            case OP_STR:
                uint16_t sr      = (instr >> 6) & 0x7;
                uint16_t BaseR   = (instr >> 6) & 0x7;
                uint16_t offset6 = sign_extend(instr & 0x1FF, 6);

                reg[sr] = mem_read(offset6 + BaseR);

                update_flags(dr);

                break;
            case OP_NOT:
                uint16_t dr = (instr >> 9) & 0x7;   // destination register
                uint16_t sr = (instr >> 6) & 0x7;   // source register

                reg[dr] = ~(reg[sr]);

                update_flags(dr);

                break;
            case OP_LDI:
                uint16_t dr        = (instr >> 9) & 0x7;
                uint16_t PCoffset9 = sign_extend(instr & 0x1FF, 9);

                reg[dr] = mem_read(mem_read(PCoffset9 + reg[RG_PC]));

                update_flags(dr);

                break;
            case OP_STI:
                uint16_t sr        = (instr >> 6) & 0x7;
                uint16_t PCoffset9 = sign_extend(instr & 0x1FF, 9);     // 9-bit value that indicates where to load the address when added to RG_PC

                reg[sr] = mem_read(mem_read(PCoffset9 + reg[RG_PC]));

                update_flags(dr);

                break;
            case OP_JMP:
                uint16_t BaseR = (instr >> 6) & 0x7;

                reg[RG_PC] = reg[BaseR];

                update_flags(dr);

                break;
            case OP_LEA:
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t PCoffset9 = sign_extend(instr & 0x1FF, 9);

                reg[dr] = reg[RG_PC] + PCoffset9;

                update_flags(dr);

                break;
            case OP_TRAP:
                //{TRAP};
                break;
            case OP_RES:            // reserved
            case OP_RTI:            // unused
            default:
                //{BAD_OPCODE}
                break;
        }
    }
    restore_input_buffering();  // if shutdown then restore terminal settings
}
