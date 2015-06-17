#ifndef __EVENT_H_
#define __EVENT_H_

#include "engine.h"

namespace event
{
    extern void run(int type, const char *arg = NULL);

    enum
    {
        // PLAYER
        PLAYER_CONNECT, PLAYER_DISCONNECT, PLAYER_RENAME, PLAYER_JOIN_SPEC, PLAYER_LEAVE_SPEC,
        PLAYER_SWITCH_TEAM, PLAYER_TEXT, PLAYER_TEAM_TEXT, PLAYER_FRAG, PLAYER_TEAM_KILL,
        PLAYER_PING_UPDATE,

        // CTF
        FLAG_SCORE, FLAG_DROP, FLAG_TAKE, FLAG_RETURN, FLAG_RESET, COAF,

        // COLLECT
        SKULL_SCORE, SKULL_TAKE, COAS,

        // CAPTURE
        BASE_CAPTURED, BASE_LOST, COAB,

        // MISC OTHERS
        STARTUP, SHUTDOWN, FRAME, MAPSTART, INTERMISSION, SERVER_MSG, MASTER_UPDATE,
        MASTERMODE_UPDATE, CONSOLE_INPUT, IPIGNORELIST, SERVCMD,

        // DEMOPLAYBACK
        DEMO_START, DEMO_END, DEMOTIME,

        // EXTINFO
        EXTINFO_UPDATE,

        // NETWORK
        CONNECT, DISCONNECT, BANDWIDTH_UPDATE,

        // DEMO
        CLIENT_DEMO_START, CLIENT_DEMO_END,

        // GUI
        SHOW_GUI, CLOSE_GUI,

        NUMEVENTS
    };

    const char *const EVENTNAMES[] =
    {
        // PLAYER
        "playerconnect", "playerdisconnect", "playerrename", "playerjoinspec", "playerleavespec",
        "playerswitchteam", "playertext", "playerteamtext", "playerfrag", "playerteamkill",
        "playerpingupdate",

        // CTF
        "flagscore", "flagdrop", "flagtake", "flagreturn", "flagreset", "coaf",

        // COLLECT
        "skullscore", "skulltake", "coas",

        // CAPTURE
        "basecaptured", "baselost", "coab",

        // MISC OTHER
        "startup", "shutdown", "frame", "mapstart", "intermission", "servmsg", "masterupdate",
        "mastermodeupdate", "consoleinput", "ipignorelist", "servcmd",

        // DEMOPLAYBACK
        "demostart", "demoend", "demotime",

        // EXTINFO
        "extinfoupdate",

        // NETWORK
        "connect", "disconnect", "bandwidthupdate",

        // DEMO
        "clientdemostart", "clientdemoend",

        // GUI
        "showgui", "closegui"
    };

}

#endif // __EVENT_H_
