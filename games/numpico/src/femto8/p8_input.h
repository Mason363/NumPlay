/**
 * Copyright (C) 2026 Chris January, Ben Baker, and contributors
 *
 * Changed for NumPico: only PICO-8's buttons (the calculator's keys are read in pico.c).
 */

#ifndef P8_INPUT_H
#define P8_INPUT_H

#include "p8_emu.h"

enum
{
    BUTTON_LEFT = 0,
    BUTTON_RIGHT = 1,
    BUTTON_UP = 2,
    BUTTON_DOWN = 3,
    BUTTON_ACTION1 = 4,
    BUTTON_ACTION2 = 5,
    BUTTON_PAUSE = 6,
};

enum
{
    BUTTON_MASK_LEFT      = (1 << BUTTON_LEFT),
    BUTTON_MASK_RIGHT     = (1 << BUTTON_RIGHT),
    BUTTON_MASK_UP        = (1 << BUTTON_UP),
    BUTTON_MASK_DOWN      = (1 << BUTTON_DOWN),
    BUTTON_MASK_ACTION1   = (1 << BUTTON_ACTION1),
    BUTTON_MASK_ACTION2   = (1 << BUTTON_ACTION2),
    BUTTON_MASK_PAUSE     = (1 << BUTTON_PAUSE),
};

extern uint16_t m_buttons[PLAYER_COUNT];
extern uint16_t m_buttonsp[PLAYER_COUNT];

#endif
