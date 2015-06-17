#ifndef __DEMO_H_
#define __DEMO_H_

#include "game.h"

namespace demo
{
    extern bool isrecording;

    extern void setup(const char *filename = NULL);
    extern void ctfinit(ucharbuf &p);
    extern void captureinit(ucharbuf &p);
    extern void collectinit(ucharbuf &p);

    extern void _putdemoclientip(int type, int cn, uint ip);

    extern ENetPacket *packet(int chan, const ENetPacket *p);
    extern void packet(int chan, const ucharbuf &p);
    extern void addmsghook(int type, int cn, const ucharbuf &p);
    extern void sayteam(const char *msg);
    extern void clipboard(int plainlen, int packlen, const uchar *data);
    extern void explodefx(int cn, int gun, int id);
    extern void shotfx(int cn, int gun, int id, const vec &from, const vec &to);

    extern void stop(bool askkeep = true);

    extern int demoauto,
               demoautonotlocal;
    extern void keepdemo(int *keep);

    namespace preview
    {
        struct extplayer
        {
            string name,
                   team;
            int cn,
                ping,
                frags,
                flags,
                deaths,
                teamkills,
                accuracy,
                privilege,
                gunselect,
                state,
                playermodel;
            string ip,
                   country;

            extplayer() : cn(-1), ping(0), frags(0), flags(0), deaths(0), teamkills(0), accuracy(0), privilege(0), gunselect(4), state(0), playermodel(0)
            {
                name[0] = team[0] = ip[0] = country[0] = '\0';
            }
            ~extplayer()
            {
                cn = -1;
                ping = 0;
                name[0] = team[0] = ip[0] = country[0] = '\0';
            }

            static bool compare(extplayer * a, extplayer * b)
            {
                if(a->flags > b->flags) return true;
                if(a->flags < b->flags) return false;
                if(a->frags > b->frags) return true;
                if(a->frags < b->frags) return false;
                return a->name[0] && b->name[0] ? strcmp(a->name, b->name) > 0 : false;
            }
        };

        struct extteam
        {
            string name;
            int score,
                frags,
                numbases;
            vector<extplayer *> players;

            extteam() : score(0), frags(0), numbases(-1) { name[0] = 0; }
            extteam(char *in) : score(0), frags(0), numbases(-1)
            {
                if(in && in[0]) copystring(name, in);
            }
            ~extteam()
            {
                name[0] = 0;
                score = frags = numbases = -1;
            }

            char *isclan()
            {
                string clanname;
                int members = 0;
                loopv(players)
                {
                    if(members) { if(strstr(players[i]->name, clanname)) members++; }
                    else loopvk(game::clantags)
                    {
                        if(strstr(players[i]->name, game::clantags[k]->name))
                        {
                            strcpy(clanname, game::clantags[k]->name);
                            members ++;
                        }
                    }
                }
                return (members*3) >= (players.length()*2) && players.length()>=2 ? newstring(clanname) : NULL;
            }

            static bool compare(extteam *a, extteam *b)
            {
                if(!a->name[0])
                {
                    if(b->name[0]) return false;
                }
                else if(!b->name[0]) return true;
                if(a->score > b->score) return true;
                if(a->score < b->score) return false;
                if(a->players.length() > b->players.length()) return true;
                if(a->players.length() < b->players.length()) return false;
                return a->name[0] && b->name[0] ? strcmp(a->name, b->name) > 0 : false;
            }
        };

        extern int sortterm;
        extern bool upsidedown;
        enum
        {
            SORT_FILE = 1,
            SORT_TYPE,
            SORT_INFO,
            SORT_MODE,
            SORT_MAP
        };

        extern const char *listteams(g3d_gui *cgui, vector<extteam *> &teams, int mode, bool icons, bool forcealive, bool frags, bool deaths, bool tks, bool acc, bool flags, bool cn, bool ping = false);
        extern const char *listspectators(g3d_gui *cgui, vector<extplayer *> &spectators, bool cn = true, bool ping = false);
    }
}

extern string homedir;

#endif // __DEMO_H_
