// frank-war1: shared decoding for the unit-script interpreters (0x10473).
// SPDX-License-Identifier: Zlib
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t next;
    uint8_t opcode, operand, frame, wait;
} WarScriptInstruction;

static inline bool wscr_countdown(uint8_t* wait)
{
    --*wait;
    return *wait == 0;
}

static inline bool wscr_decode(const uint8_t* data, uint32_t length, uint32_t pc,
                               uint8_t speed, bool slowed, WarScriptInstruction* instruction)
{
    if (pc >= length) return false;
    const uint8_t op = data[pc];
    uint32_t size;
    switch (op)
    {
        case 0: case 2: case 8: case 9: size = 1; break;
        case 1: case 4: case 5: case 7: case 10: size = 2; break;
        case 3: case 6: size = 3; break;
        default: return false;
    }
    if (length - pc < size) return false;
    *instruction = (WarScriptInstruction){
        .next = pc + size,
        .opcode = op,
        .operand = size > 1 ? data[pc + 1] : 0
    };
    switch (op)
    {
        case 0: instruction->wait = 1; break;
        case 1: instruction->wait = instruction->operand; break;
        case 3:
            instruction->next = (uint32_t)data[pc + 1] | (uint32_t)data[pc + 2] << 8;
            if (instruction->next >= length) return false;
            break;
        case 4: instruction->frame = instruction->operand; break;
        case 6: instruction->frame = data[pc + 2]; break;
        case 7: instruction->wait = (uint8_t)(instruction->operand - (speed & 15)); break;
        case 10: instruction->wait = (uint8_t)(instruction->operand + slowed); break;
        default: break;
    }
    return true;
}
