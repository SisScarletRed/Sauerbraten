#include "mod.h"

using namespace game;

char filename_[1024];
void sendfile_(char *filename)
{
    if(game::gamemode!=1 || !filename) return;

    stream *file = openrawfile(path(filename), "rb");
    if(!file) return;

    sendfile(-1, 2, file);
    conoutf("\f0file %s sent", filename);
    delete file;
}
QCOMMANDN(sendfile, "sends a normal file to a coop-edit server, so that supporting clients can download it", "file", sendfile_, "s");

void getfile_(char *filename)
{
    strcpy(filename_, filename);
    if(*filename==0) return;

    conoutf("\f0Getting file %s", filename_);
    game::addmsg(N_GETMAP, "r");
}
QCOMMANDN(getfile, "get a normal file. \f3be careful so it won't harm your PC!", "file", getfile_, "s");

QICOMMAND(getskill, "returns the skill of a bot", "cn", "i", (int *botcn), {
            if(*botcn < 128 || !game::getclient(*botcn)) return;
            fpsent *d = game::getclient(*botcn);
            intret(d->skill);
         });

// CHEAT-DETECTION [ B E T A ]

void detected_cheat(const char *desc, int safety, fpsent *c)
{
    if((!desc && !desc[0]) || !c) return;

    safety = c->ping > 300 ? AC_SUSPECT_HIGHPING : player1->ping > 300 ? AC_PLAYER1_HIGHPING : safety;

    conoutf("\f7\fs\f3Detected cheat\fr: %s. \fs\f3Cheater\fr: %s(%d) \fs\f3Safety\fr: %d (0-32) \n\t\f0NOTE: THIS IS STILL ABSOLUTELY BETA!!",
            desc, c->name, c->clientnum, safety);
}

