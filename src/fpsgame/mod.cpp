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

VARP(fragmsgdeaths, 0, 1, 1);
VARP(fragmsgname, 0, 1, 1);
VARP(fragmsgsize, 1, 4, 8);
VARP(fragmsgposy, 0, 50, 1000);

// FRAGMSG

namespace game { extern int fragmsgfade; }

void drawfragmsg(fpsent *d, int w, int h)
{
    #define WEAP_ICON_SL 64
    #define WEAP_ICON_SPACE 20
    #define ICON_TEXT_DIFF 4

    string buf1, buf2;
    fpsent *att, *vic;
    int fragtime, weapon,
        msg1w, msg1h, msg2w, msg2h, total_width = 0,
        msg1posx, msgiconposx, msg2posx, msgxoffset,
        iconid;
    float alpha;
    bool suicide;

    const float fragmsgscale = 0.35+fragmsgsize/8.0,
                posy = fragmsgposy*(h/fragmsgscale-WEAP_ICON_SL)/1000-ICON_TEXT_DIFF;

    if(d->lastfragtime >= d->lastdeathtime)
    {
        att = d;
        vic = d->lastvictim;
        weapon = d->lastfragweapon;
        fragtime = d->lastfragtime;
    }
    else
    {
        if(!fragmsgdeaths) return;
        att = d->lastkiller;
        vic = d;
        weapon = d->lastdeathweapon;
        fragtime = d->lastdeathtime;
    }

    suicide = (att==vic);
    if(!fragmsgdeaths && suicide) return;
    iconid = (weapon>-1) ? HICON_FIST+weapon : HICON_TOKEN;

    if(!suicide)
    {
        sprintf(buf1, "%s", teamcolorname(att, (!fragmsgname && att==d) ? "You" : att->name));
        text_bounds(buf1, msg1w, msg1h);
        total_width += msg1w+WEAP_ICON_SPACE;
    }

    sprintf(buf2, "%s", teamcolorname(vic, (!fragmsgname && vic==d) ? "You" : vic->name));
    text_bounds(buf2, msg2w, msg2h);
    total_width += msg2w+WEAP_ICON_SL+WEAP_ICON_SPACE;

    msgxoffset = (total_width*(1-fragmsgscale));
    msg1posx = (w-total_width+msgxoffset)/(2*fragmsgscale);
    msgiconposx = (suicide) ? msg1posx : msg1posx+msg1w+WEAP_ICON_SPACE;
    msg2posx = msgiconposx+WEAP_ICON_SL+WEAP_ICON_SPACE;

    alpha = 255;
    if(lastmillis-fragtime>fragmsgfade)
        alpha = 255-(lastmillis-fragtime)+fragmsgfade;
    alpha = max(alpha, 0.0f);

    glPushMatrix();
    glScalef(fragmsgscale, fragmsgscale, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, alpha/255);
    if(!suicide) draw_text(buf1, msg1posx, posy, 255, 255, 255, alpha);
    drawicon(iconid, msgiconposx, posy+ICON_TEXT_DIFF, WEAP_ICON_SL);
    draw_text(buf2, msg2posx, posy, 255, 255, 255, alpha);
    glPopMatrix();
}

// CHEAT-DETECTION [ B E T A ]

void detected_cheat(const char *desc, int safety, fpsent *c)
{
    if((!desc && !desc[0]) || !c) return;

    safety = c->ping > 300 ? AC_SUSPECT_HIGHPING : player1->ping > 300 ? AC_PLAYER1_HIGHPING : safety;

    conoutf("\f7\fs\f3Detected cheat\fr: %s. \fs\f3Cheater\fr: %s(%d) \fs\f3Safety\fr: %d (0-32) \n\t\f0NOTE: THIS IS STILL ABSOLUTELY BETA!!",
            desc, c->name, c->clientnum, safety);
}

