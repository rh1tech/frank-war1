// SPDX-License-Identifier: Zlib
#include <assert.h>
#include "../port/war_script_frank.h"

static void expectDelay(uint8_t wait, unsigned ticks)
{
    for (unsigned t = 1; t < ticks; t++) assert(!wscr_countdown(&wait));
    assert(wscr_countdown(&wait));
}

static void sharedBlockAndStop(void)
{
    const uint8_t script[] = { 4, 99, 1, 2, 0, 2, 1, 0, 3, 2, 0 };
    WarScriptInstruction instruction;
    uint32_t pc = 8;
    assert(wscr_decode(script, sizeof(script), pc, 0, false, &instruction));
    assert(instruction.opcode == 3 && instruction.next == 2);
    pc = instruction.next;
    assert(wscr_decode(script, sizeof(script), pc, 0, false, &instruction));
    assert(instruction.opcode == 1 && instruction.next == 4);
    expectDelay(instruction.wait, 2);
    pc = instruction.next;
    assert(wscr_decode(script, sizeof(script), pc, 0, false, &instruction));
    assert(instruction.opcode == 0 && instruction.next == 5);
    expectDelay(instruction.wait, 1);
    pc = instruction.next;
    assert(wscr_decode(script, sizeof(script), pc, 0, false, &instruction));
    assert(instruction.opcode == 2 && instruction.next == 6);
    expectDelay(instruction.wait, 256);
    pc = instruction.next;
    assert(wscr_decode(script, sizeof(script), pc, 0, false, &instruction));
    assert(instruction.opcode == 1 && instruction.next == 8);
    expectDelay(instruction.wait, 256);
}

static void wrappedModifiers(void)
{
    const uint8_t script[] = { 7, 1, 10, 255, 6, 254, 19 };
    WarScriptInstruction instruction;
    assert(wscr_decode(script, sizeof(script), 0, 1, false, &instruction));
    expectDelay(instruction.wait, 256);
    assert(wscr_decode(script, sizeof(script), 0, 2, false, &instruction));
    expectDelay(instruction.wait, 255);
    assert(wscr_decode(script, sizeof(script), 2, 0, true, &instruction));
    expectDelay(instruction.wait, 256);
    assert(wscr_decode(script, sizeof(script), 2, 0, false, &instruction));
    expectDelay(instruction.wait, 255);
    assert(wscr_decode(script, sizeof(script), 4, 0, false, &instruction));
    assert((int8_t)instruction.operand == -2 && instruction.frame == 19 && instruction.next == 7);
}

static void invalidInstructions(void)
{
    const uint8_t script[] = { 3, 6, 0, 11, 1 };
    WarScriptInstruction instruction;
    assert(!wscr_decode(script, sizeof(script), 0, 0, false, &instruction));
    assert(!wscr_decode(script, sizeof(script), 3, 0, false, &instruction));
    assert(!wscr_decode(script, sizeof(script), 4, 0, false, &instruction));
    assert(!wscr_decode(script, sizeof(script), 5, 0, false, &instruction));
    assert(!wscr_decode(script, 2, 0, 0, false, &instruction));
}

int main(void)
{
    sharedBlockAndStop();
    wrappedModifiers();
    invalidInstructions();
    return 0;
}
