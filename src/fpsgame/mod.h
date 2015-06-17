#ifndef __MOD_H_
#define __MOD_H_

#include "game.h"

// cheat-detection safety
enum
{
    AC_TESTING = 0,
    AC_PLAYER1_HIGHPING = 1,
    AC_SUSPECT_HIGHPING = 2,
    AC_LOW = 4,
    AC_MEDIUM = 8,
    AC_HIGH = 16,
    AC_SURE = 32
};

extern void detected_cheat(const char *desc, int safety, fpsent *c);

#endif // __MOD_H_
