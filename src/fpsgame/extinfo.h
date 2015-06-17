#ifndef __EXTINFO_H
#define __EXTINFO_H

#define EXT_ACK                         -1
#define EXT_VERSION                     105
#define EXT_NO_ERROR                    0
#define EXT_ERROR                       1
#define EXT_PLAYERSTATS_RESP_IDS        -10
#define EXT_PLAYERSTATS_RESP_STATS      -11
#define EXT_UPTIME                      0
#define EXT_PLAYERSTATS                 1
#define EXT_TEAMSCORE                   2

#ifndef MAXTEAMS
#define MAXTEAMS 128
#endif

#include "engine.h"

namespace extinfo
{
    extern void cleangameinfo(void *p);

    /**
     *  Server info
     */

    struct serverinfodata
    {
        enum
        {
            WAITING = INT_MAX,
            MAXPINGS = 3
        };

        string name, map, sdesc;
        int ping, port, numplayers;
        ENetAddress address;
        vector<int> attr;
        void *gameinfo;

        serverinfodata()
        {
            name[0] = map[0] = sdesc[0] = '\0';
            ping = WAITING;
            port = -1;
            numplayers = 0;
            address.host =  ENET_HOST_ANY;
            address.port =  ENET_PORT_ANY;
            gameinfo = NULL;
        }

        ~serverinfodata()
        {
            if(gameinfo) cleangameinfo(gameinfo);
        }

        bool iswaiting()
        {
            return ping == WAITING;
        }
    };

    /**
     *  General Extinfo stuff
     */

    #define MAXSERVSTRING 100

    struct serverdata
    {
        int ping, nclients, mode, timelimit, maxclients,
            access, gamepaused, gamespeed;
        char servname[MAXSERVSTRING], description[MAXSERVSTRING];

        serverdata()
        {
            ping = nclients = mode = timelimit = maxclients = access = 0;
            gamepaused = gamespeed = servname[0] = description[0] = 0;
        }

        void reset()
        {
            ping = nclients = mode = timelimit = maxclients = access = 0;
            gamepaused = gamespeed = servname[0] = description[0] = 0;
        }

        void update(struct serverdata &ndata)
        {
            ping = ndata.ping;
            nclients = ndata.nclients;
            mode = ndata.mode;
            timelimit = ndata.timelimit;
            maxclients = ndata.maxclients;
            access = ndata.access;
            gamepaused = ndata.gamepaused;
            gamespeed = ndata.gamespeed;
            strncpy(servname, ndata.servname, MAXSERVSTRING-1);
            servname[MAXSERVSTRING-1] = 0;
            strncpy(description, ndata.description, MAXSERVSTRING-1);
            description[MAXSERVSTRING-1] = 0;
        }
    };

    #define MAXEXTTEAMLENGHT 5

    struct teamdata
    {
        char teamname[MAXEXTTEAMLENGHT];
        int score, bases;

        teamdata()
        {
            teamname[0] = score = bases = 0;
        }

        void update(const struct teamdata &ndata)
        {
            strncpy(teamname, ndata.teamname, MAXEXTTEAMLENGHT-1);
            teamname[MAXEXTTEAMLENGHT-1] = 0;
            score = ndata.score;
            bases = ndata.bases;
        }

        teamdata(const struct teamdata &ndata) { update(ndata); }
    };

    struct teamsinfo
    {
        int notteammode, gamemode, timeleft, nteams;
        struct teamdata teams[MAXTEAMS];

        teamsinfo()
        {
            notteammode = gamemode = timeleft = nteams = 0;
        }

        void update(const struct teamsinfo& ndata)
        {
            notteammode = ndata.notteammode;
            gamemode = ndata.gamemode;
            timeleft = ndata.timeleft;
            nteams = ndata.nteams;
            loopi(ndata.nteams) teams[i].update(ndata.teams[i]);
        }

        teamsinfo(const struct teamsinfo& ndata) { update(ndata); }

        void reset()
        {
            notteammode = gamemode = timeleft = nteams = 0;
        }

        void addteam(struct teamdata &td)
        {
            if(nteams>=MAXTEAMS) return;
            teams[nteams].update(td);
            nteams++;
        }
    };

    #define MAXEXTNAMELENGHT 16

    struct extplayerdata
    {
        int cn, ping;
        char name[MAXEXTNAMELENGHT], team[MAXEXTTEAMLENGHT];
        int frags, flags, deaths, teamkills, acc, health,
            armour, gunselect, privilege, state;
        uint ip;
        int lastseen;

        extplayerdata()
        {
            cn = ping = name[0] = team[0] = frags = flags = 0;
            deaths = teamkills = acc = health = armour = 0;
            gunselect = privilege = state = 0;
            ip = 0;
            lastseen = totalmillis;
        }

        void reset()
        {
            cn = ping = name[0] = team[0] = frags = flags = 0;
            deaths = teamkills = acc = health = armour = 0;
            gunselect = privilege = state = 0;
            ip = 0;
            lastseen = totalmillis;
        }

        void update(const struct extplayerdata& ndata)
        {
            cn = ndata.cn;
            ping = ndata.ping;
            strncpy(name, ndata.name, MAXEXTNAMELENGHT-1);
            name[MAXEXTNAMELENGHT-1] = 0;
            strncpy(team, ndata.team, MAXEXTTEAMLENGHT-1);
            team[MAXEXTTEAMLENGHT-1] = 0;
            frags = ndata.frags;
            flags = ndata.flags;
            deaths = ndata.deaths;
            teamkills = ndata.teamkills;
            acc = ndata.acc;
            health = ndata.health;
            armour = ndata.armour;
            gunselect = ndata.gunselect;
            privilege = ndata.privilege;
            state = ndata.state;
            ip = ndata.ip;
            lastseen = totalmillis;
        }

        extplayerdata(const struct extplayerdata &ndata) { update(ndata); }
    };

    #define MAXPREVIEWPLAYERS 128
    #define SERVUPDATEINTERVAL 3000
    #define SERVUPDATETEAMGAP 500

    struct serverpreviewdata
    {
        struct serverdata sdata;
        struct extplayerdata players[MAXPREVIEWPLAYERS];
        int nplayers;
        struct teamsinfo tinfo;
        ENetAddress servaddress;
        bool isupdating, hasserverdata, hasplayerdata;
        int lastupdate, lastteamupdate;

        serverpreviewdata()
        {
            isupdating = hasserverdata = hasplayerdata = false;
            lastupdate = lastteamupdate = 0;
            sdata.reset();
            tinfo.reset();
            servaddress.host = servaddress.port = nplayers = 0;
        }

        void reset()
        {
            isupdating = hasserverdata = hasplayerdata = false;
            lastupdate = lastteamupdate = 0;
            sdata.reset();
            tinfo.reset();
            servaddress.host = servaddress.port = nplayers = 0;
        }

        void addplayer(struct extplayerdata& data)
        {
            if(nplayers >= MAXPREVIEWPLAYERS) return;
            bool found = false;

            loopi(nplayers)
            {
                if(players[i].cn == data.cn)
                {
                    players[i].update(data);
                    found = true;
                }
            }

            if(!found)
            {
                players[nplayers].update(data);
                nplayers++;
            }
        }

        void removeplayer(int n)
        {
            if(nplayers > 0 && n < nplayers)
            {
                nplayers--;
                loopi(nplayers-n) players[n+i] = players[n+i+1];
            }
        }

        void checkdisconnected(int timeout)
        {
            loopi(nplayers) { if(players[i].lastseen + timeout < totalmillis) removeplayer(i); }
        }
    };

    #define MAXEXTRETRIES 2
    #define EXTRETRIESINT 500
    #define EXTREFRESHINT 3000

    struct extplayerinfo
    {
        bool finished;
        int lastattempt, attempts;
        bool needrefresh;
        struct extplayerdata data;

        extplayerinfo()
        {
            finished = needrefresh = false;
            attempts = 0;
            lastattempt = totalmillis;
        }

        extplayerinfo(bool refresh)
        {
            finished = false;
            needrefresh = refresh;
            attempts = 0;
            lastattempt = totalmillis;
        }

        void resetextdata()
        {
            finished = false;
            attempts = 0;
            lastattempt = totalmillis;
            data.reset();
        }

        void setextplayerinfo()
        {
            attempts = 0;
            finished = true;
        }

        void setextplayerinfo(struct extplayerdata ndata)
        {
            attempts = 0;
            finished = true;
            data.update(ndata);
        }

        void addattempt()
        {
            attempts++;
            lastattempt = totalmillis;
        }

        bool needretry()
        {
            if(needrefresh)return lastattempt + EXTREFRESHINT < totalmillis;
            else return !finished && attempts <= MAXEXTRETRIES && lastattempt + EXTRETRIESINT < totalmillis;
        }

        bool isfinal() { return !needrefresh && (finished || attempts > MAXEXTRETRIES); }

        int getextplayerinfo()
        {
            if(!finished) return -1;
            return 0;
        }

        int getextplayerinfo(struct extplayerdata& ldata)
        {
            if(!finished) return -1;
            ldata.update(data);
            return 0;
        }
    };

    /**
     *  Players-GUi
     */

    struct playersentry
    {
        const char *pname, *sdesc, *shost;
        int sping, splayers, smaxplayers, smode, sicon, sport;
        uint sip;

        playersentry()
        {
            pname = sdesc = shost = "";
            sping = splayers = smaxplayers = smode = sicon = sip = sport = 0;
        }

        playersentry(const char *name, const char *desc, const char *host,
                     int ping, int players, int maxplayers, int mode, int icon, uint ip, int port)
        {
            pname = name;
            sdesc = desc;
            shost = host;
            sping = ping;
            splayers = players;
            smaxplayers = maxplayers;
            smode = mode;
            sicon = icon;
            sip = ip;
            sport = port;
        }
    };

    /**
     *  General
     */

    extern void update();
    extern void requestgameinfo(ENetAddress address);
    extern void setserverpreview(const char *servername, int serverport);
    extern void requestplayer(int cn);
}

namespace whois
{
    extern void addplayerwhois(int cn);
    extern void addwhoisentry(uint ip, const char *name);
    extern void loadwhoisdb();
    extern void writewhoisdb();
    extern const char *getwhoisnames(const int cn);
    extern void whois(const int cn);
}

extern vector<extinfo::serverinfodata *> getservers();

extern void saveservergameinfo(ENetAddress address, void *pdata);
extern void *getservergameinfo(ENetAddress address);
extern void refreshservers();
extern void connectserver(const char *host, int port);

extern void forceinitservers();

#endif // __EXTINFO_H
