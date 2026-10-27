/*
 * PS5 pad button bits, as carried in the virtual pad report's button word.
 */

#ifndef SCE_PAD_H
#define SCE_PAD_H

#define SCE_PAD_BUTTON_L3            0x00000002
#define SCE_PAD_BUTTON_R3            0x00000004
#define SCE_PAD_BUTTON_OPTIONS       0x00000008
#define SCE_PAD_BUTTON_UP            0x00000010
#define SCE_PAD_BUTTON_RIGHT         0x00000020
#define SCE_PAD_BUTTON_DOWN          0x00000040
#define SCE_PAD_BUTTON_LEFT          0x00000080
#define SCE_PAD_BUTTON_L2            0x00000100
#define SCE_PAD_BUTTON_R2            0x00000200
#define SCE_PAD_BUTTON_L1            0x00000400
#define SCE_PAD_BUTTON_R1            0x00000800
#define SCE_PAD_BUTTON_TRIANGLE      0x00001000
#define SCE_PAD_BUTTON_CIRCLE        0x00002000
#define SCE_PAD_BUTTON_CROSS         0x00004000
#define SCE_PAD_BUTTON_SQUARE        0x00008000
#define SCE_PAD_BUTTON_PS            0x00010000  /* verified: opens control center */
#define SCE_PAD_BUTTON_TOUCH_PAD     0x00100000
#define SCE_PAD_BUTTON_CREATE        0x00200000  /* unverified */

#endif /* SCE_PAD_H */
