/* Analog movement's state that carries from frame to frame (analog_move.c): in its own unit so that save states and
 * rewinds keep it (build_boot.py STATE_HOST_UNITS: its .data is game state). */
#ifndef ANALOG_STATE_H
#define ANALOG_STATE_H

#define PC_ANALOG_ACTORS 17

struct pc_analog_state {
    int sticky;                              /* the last movement input was the stick (not the D-pad) */
    int latch;                               /* the tile direction interaction last chose (-1: none) */
    int anim_acc;                            /* Ashley's stride playback fraction (1/256 frame) */
    int remainders[PC_ANALOG_ACTORS][2];     /* each actor's lost step fractions (units / 0x1000000) */
};
extern struct pc_analog_state pc_analog_st;

#endif
