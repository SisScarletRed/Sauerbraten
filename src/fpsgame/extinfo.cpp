// extinfo functions..

#include "game.h"
#include "extinfo.h"
#include "event.h"
#include "ipignore.h"

struct serverinfo;

extern int showserverpreviews;
extern serverinfo *selectedserver;
extern void connectselected();

using namespace game;
namespace extinfo
{
    /**
     *  General Extinfo stuff
     */

    ENetSocket extinfosock = ENET_SOCKET_NULL;

    ENetSocket getextsock()
    {
        if(extinfosock != ENET_SOCKET_NULL) return extinfosock;

        extinfosock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        enet_socket_set_option(extinfosock, ENET_SOCKOPT_NONBLOCK, 1);
        enet_socket_set_option(extinfosock, ENET_SOCKOPT_BROADCAST, 1);
        return extinfosock;
    }

    void requestplayer(int cn)
    {
        const ENetAddress *paddress = connectedpeer();
        if(!paddress) return;

        ENetAddress address = *paddress;
        ENetSocket extsock = getextsock();
        if(extsock == ENET_SOCKET_NULL) return;

        address.port = server::serverinfoport(address.port);
        ENetBuffer buf;

        uchar send[MAXTRANS];
        ucharbuf p(send, MAXTRANS);

        putint(p, 0);
        putint(p, EXT_PLAYERSTATS);
        putint(p, cn);

        buf.data = send;
        buf.dataLength = p.length();
        enet_socket_send(extsock, &address, &buf, 1);
    }

    int playerparser(ucharbuf p, struct extplayerdata& data)
    {
        char strdata[MAXTRANS];

        getint(p);

        if(getint(p) != EXT_ACK || getint(p) != EXT_VERSION) return -2;

        int err = getint(p);
        if(err) return -3;

        if(getint(p) != EXT_PLAYERSTATS_RESP_STATS) return -4;

        data.cn = getint(p);
        data.ping = getint(p);

        getstring(strdata, p);
        strncpy(data.name, strdata, MAXEXTNAMELENGHT-1);
        data.name[MAXEXTNAMELENGHT-1] = 0;

        getstring(strdata, p);
        strncpy(data.team, strdata, MAXEXTTEAMLENGHT-1);
        data.team[MAXEXTTEAMLENGHT-1] = 0;

        data.frags = getint(p);
        data.flags = getint(p);
        data.deaths = getint(p);
        data.teamkills = getint(p);
        data.acc = getint(p);
        data.health = getint(p);
        data.armour = getint(p);
        data.gunselect = getint(p);
        data.privilege = getint(p);
        data.state = getint(p);

        p.get((uchar *)&data.ip, 3);
        if(data.ip)
        {
            whois::addwhoisentry(data.ip, data.name);
            demo::_putdemoclientip(N_CDEMO_CLIENTIP, data.cn, data.ip);
        }

        return 0;
    }

    int extplayershelper(ucharbuf p, struct extplayerdata& data)
    {
        if(getint(p) != 0 || getint(p) != EXT_PLAYERSTATS) return -1;
        return playerparser(p, data);
    }

    void process()
    {
        const ENetAddress *paddress = connectedpeer();
        if(!paddress) return;

        ENetAddress connectedaddress = *paddress;
        ENetSocket extsock = getextsock();
        if(extsock == ENET_SOCKET_NULL) return;

        enet_uint32 events = ENET_SOCKET_WAIT_RECEIVE;
        int s = 0;

        ENetBuffer buf;
        ENetAddress address;
        uchar data[MAXTRANS];
        buf.data = data;
        buf.dataLength = sizeof(data);

        while((s = enet_socket_wait(extsock, &events, 0)) >= 0 && events)
        {
            int len = enet_socket_receive(extsock, &address, &buf, 1);
            if(len <= 0 || connectedaddress.host != address.host ||
               server::serverinfoport(connectedaddress.port) != address.port) continue;

            ucharbuf p(data, len);
            struct extplayerdata extpdata;
            if(!extplayershelper(p, extpdata))
            {
                fpsent *d = getclient(extpdata.cn);
                if(!d || d->extdata.isfinal()) continue;

                d->extdata.setextplayerinfo(extpdata);
                if(!d->extdatawasinit)
                {
                    d->deaths = extpdata.deaths;
                    d->extdatawasinit = true;
                }
            }
        }
    }

    ENetSocket servinfosock = ENET_SOCKET_NULL;
    struct serverpreviewdata lastpreviewdata;

    ENetSocket getservsock()
    {
        if(servinfosock != ENET_SOCKET_NULL) return servinfosock;

        servinfosock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        enet_socket_set_option(servinfosock, ENET_SOCKOPT_NONBLOCK, 1);
        enet_socket_set_option(servinfosock, ENET_SOCKOPT_BROADCAST, 1);
        return servinfosock;
    }

    int serverinfoparser(ucharbuf p, int millis, struct serverdata& data)
    {
        char strdata[MAXTRANS];
        data.ping = (totalmillis - millis);
        data.nclients = getint(p);

        int nargs = getint(p);
        if(getint(p) != PROTOCOL_VERSION) return -2;

        data.mode = getint(p);
        data.timelimit = getint(p);
        data.maxclients = getint(p);
        data.access = getint(p);
        if(nargs >= 7)
        {
            data.gamepaused = getint(p);
            data.gamespeed = getint(p);
        }
        else
        {
            data.gamepaused = 0;
            data.gamespeed = 100;
        }

        getstring(strdata, p);
        strncpy(data.servname, strdata, MAXSERVSTRING-1);
        data.servname[MAXSERVSTRING-1] = 0;

        getstring(strdata, p);
        strncpy(data.description, strdata, MAXSERVSTRING-1);
        data.description[MAXSERVSTRING-1] = 0;
        return 0;
    }

    int teamparser(ucharbuf p, int expectedteams, struct teamsinfo& data)
    {
        char strdata[MAXTRANS];
        if(getint(p) != EXT_ACK || getint(p) != EXT_VERSION) return -2;

        data.notteammode = getint(p);
        data.gamemode = getint(p);
        data.timeleft = getint(p);
        if(data.notteammode) return 0;
        struct teamdata td;

        loopi(min(32,expectedteams))
        {
            getstring(strdata, p);
            strncpy(td.teamname, strdata, MAXEXTTEAMLENGHT-1);
            td.teamname[MAXEXTTEAMLENGHT-1] = 0;
            td.score = getint(p);
            td.bases = getint(p);
            loopj(min(32,td.bases)) getint(p);
            data.addteam(td);
        }
        return 0;
    }

    int serverinfohelper(ucharbuf p,
                         struct serverdata &sdata,
                         struct extplayerdata &pdata,
                         struct teamsinfo &tdata)
    {
        int millis = getint(p);
        int res = 0, type = 0, nteams = 0;
        char *teamnames[MAXTEAMS];
        if(millis)
        {
            type = 1;
            res = serverinfoparser(p, millis, sdata);
        }
        else
        {
            int extinfotype = getint(p);
            if(extinfotype >= 100 && type <= 102)
                type -= 100;

            if(extinfotype == EXT_PLAYERSTATS)
            {
                type = 2;
                res = playerparser(p, pdata);
            }
            else if(extinfotype == EXT_TEAMSCORE)
            {
                type = 3;
                int isold;
                loopi(lastpreviewdata.nplayers)
                {
                    isold = 0;
                    loopj(nteams)
                    {
                        isold = !strcmp(lastpreviewdata.players[i].team, teamnames[j]);
                        if(isold) break;
                    }
                    if(!isold)
                    {
                        teamnames[nteams] = new char[MAXSTRLEN];
                        copystring(teamnames[nteams], lastpreviewdata.players[i].team);
                        nteams++;
                    }
                }
                loopi(nteams) delete[] teamnames[i];
                res = teamparser(p, nteams, tdata);
            }
        }
        return res ? res : type;
    }

    void requestserverinfosend()
    {
        ENetSocket sock = getservsock();
        if(sock == ENET_SOCKET_NULL) return;
        lastpreviewdata.lastupdate = totalmillis;

        uchar send[MAXTRANS];

        ENetBuffer buf1;
        ucharbuf p1(send, MAXTRANS);

        putint(p1, totalmillis);

        buf1.data = send;
        buf1.dataLength = p1.length();
        enet_socket_send(sock, &lastpreviewdata.servaddress, &buf1, 1);

        ENetBuffer buf2;
        ucharbuf p2(send, MAXTRANS);

        putint(p2, 0);
        putint(p2, EXT_PLAYERSTATS);
        putint(p2, -1);

        buf2.data = send;
        buf2.dataLength = p2.length();
        enet_socket_send(sock, &lastpreviewdata.servaddress, &buf2, 1);
    }

    void requeststeaminfosend()
    {
        ENetSocket sock = getservsock();
        if(sock == ENET_SOCKET_NULL) return;
        lastpreviewdata.lastteamupdate = totalmillis;

        uchar send[MAXTRANS];

        ENetBuffer buf2;
        ucharbuf p2(send, MAXTRANS);

        putint(p2, 0);
        putint(p2, EXT_TEAMSCORE);

        buf2.data = send;
        buf2.dataLength = p2.length();
        enet_socket_send(sock, &lastpreviewdata.servaddress, &buf2, 1);
    }

    /**
     *  Server-preview stuff
     */

    bool waitforfreeslot = false;
    bool hasfreeslot = true;
    void setserverpreview(const char *servername, int serverport)
    {
        lastpreviewdata.reset();
        if(enet_address_set_host(&lastpreviewdata.servaddress, servername) < 0) return;
        if(serverport) lastpreviewdata.servaddress.port = server::serverinfoport(serverport);
        else lastpreviewdata.servaddress.port = SAUERBRATEN_SERVINFO_PORT;
        lastpreviewdata.isupdating = true;
        waitforfreeslot = false;
        hasfreeslot = true;
    }

    int sortplayersfn(struct extplayerdata& d1, struct extplayerdata& d2)
    {
        if(d1.flags > d2.flags) return true;
        else if(d1.flags < d2.flags) return false;
        if(d1.frags > d2.frags) return true;
        else return false;
    }
    int sortteamsfn(struct teamdata& t1, struct teamdata& t2) { return t1.score > t2.score; }

    void getseserverinfo()
    {
        ENetSocket sock = getservsock();
        if(sock == ENET_SOCKET_NULL || !lastpreviewdata.isupdating) return;

        enet_uint32 events = ENET_SOCKET_WAIT_RECEIVE;
        int s = 0;
        ENetBuffer buf;
        ENetAddress address;
        uchar data[MAXTRANS];

        buf.data = data;
        buf.dataLength = sizeof(data);

        while((s = enet_socket_wait(sock, &events, 0)) >= 0 && events)
        {
            int len = enet_socket_receive(sock, &address, &buf, 1);
            if(len <= 0 || lastpreviewdata.servaddress.host != address.host ||
               lastpreviewdata.servaddress.port != address.port) continue;

            ucharbuf p(data, len);
            struct serverdata sdata;
            struct extplayerdata pdata;
            struct teamsinfo tdata;
            int type = serverinfohelper(p, sdata, pdata, tdata);
            switch(type)
            {
                case 1:
                    lastpreviewdata.sdata.update(sdata);
                    lastpreviewdata.hasserverdata = true;
                    break;
                case 2:
                    lastpreviewdata.addplayer(pdata);
                    lastpreviewdata.checkdisconnected(SERVUPDATEINTERVAL + 2*SERVUPDATETEAMGAP);
                    break;
                case 3:
                    lastpreviewdata.tinfo.update(tdata);
                    quicksort(lastpreviewdata.tinfo.teams, lastpreviewdata.tinfo.teams+lastpreviewdata.tinfo.nteams, sortteamsfn);
                    quicksort(lastpreviewdata.players, lastpreviewdata.players+lastpreviewdata.nplayers, sortplayersfn);
                    lastpreviewdata.hasplayerdata = true;
                    break;
            }
        }
    }

    void checkseserverinfo()
    {
        if(!lastpreviewdata.isupdating) return;
        if(lastpreviewdata.lastupdate + SERVUPDATEINTERVAL < totalmillis) requestserverinfosend();
        if(lastpreviewdata.lastteamupdate + SERVUPDATEINTERVAL < totalmillis &&
           lastpreviewdata.lastupdate + SERVUPDATETEAMGAP < totalmillis) requeststeaminfosend();
        getseserverinfo();
    }

    bool isingroup(const char *name, int i) { return (name == NULL || !strcmp(lastpreviewdata.players[i].team, name)) && lastpreviewdata.players[i].state != CS_SPECTATOR; }
    bool isspec(int i) { return lastpreviewdata.players[i].state == CS_SPECTATOR; }

    bool hasspecs()
    {
        loopi(lastpreviewdata.nplayers) if(isspec(i)) return true;
        return false;
    }

    bool hasplayers(const char *groupname)
    {
        loopi(lastpreviewdata.nplayers) if(isingroup(groupname, i) && !isspec(i)) return true;
        return false;
    }

    VARP(serverpreviewflags, 0, 1, 1);
    VARP(serverpreviewfrags, 0, 1, 1);
    VARP(serverpreviewdeaths, 0, 0, 1);
    VARP(serverpreviewkpd, 0, 0, 1);
    VARP(serverpreviewacc, 0, 0, 1);
    VARP(serverpreviewcn, 0, 1, 1);
    VARP(serverpreviewping, 0, 1, 1);
    VARP(serverpreviewip, 0, 0, 1);

    void drawgroup(g3d_gui *g, const char *name)
    {
        g->pushlist();
        g->strut(15);
        g->text("name", 0x50CFE5);
        loopi(lastpreviewdata.nplayers)
        {
            if(isingroup(name, i))
            {
                int status = 0xFFFFFF;
                bool isignored = ipignore::isignored(lastpreviewdata.players[i].ip);
                if(lastpreviewdata.players[i].privilege) status = guiprivcolor(lastpreviewdata.players[i].privilege);
                if(isignored)
                {
                    g->pushlist();
                    g->background(0xC4C420);
                }
                g->text(lastpreviewdata.players[i].name, status);
                if(isignored) g->poplist();
            }
        }
        g->poplist();

        if(m_check(lastpreviewdata.sdata.mode, M_CTF) && serverpreviewflags)
        {
            g->pushlist();
            g->strut(6);
            g->text("flags", 0x50CFE5);
            for(int i=0; i<lastpreviewdata.nplayers; i++) { if(isingroup(name, i)) g->textf("%d", 0xFFFFFF, NULL, NULL, lastpreviewdata.players[i].flags); }
            g->poplist();
        }

        if(serverpreviewfrags)
        {
            g->pushlist();
            g->strut(6);
            g->text("frags", 0x50CFE5);
            loopi(lastpreviewdata.nplayers) { if(isingroup(name, i)) g->textf("%d", 0xFFFFFF, NULL, NULL, lastpreviewdata.players[i].frags); }
            g->poplist();
        }

        if(serverpreviewdeaths)
        {
            g->pushlist();
            g->strut(6);
            g->text("deaths", 0x50CFE5);
            loopi(lastpreviewdata.nplayers) { if(isingroup(name, i)) g->textf("%d", 0xFFFFFF, NULL, NULL, lastpreviewdata.players[i].deaths); }
            g->poplist();
        }

        if(serverpreviewkpd)
        {
            g->pushlist();
            g->strut(6);
            g->text("kpd", 0x50CFE5);
            loopi(lastpreviewdata.nplayers)
            {
                if(isingroup(name, i))
                {
                    float kpd = float(lastpreviewdata.players[i].frags)/max(float(lastpreviewdata.players[i].deaths), 1.0f);
                    g->textf("%4.2f", 0xFFFFFF, NULL, NULL, kpd);
                }
            }
            g->poplist();
        }

        if(serverpreviewacc)
        {
            g->pushlist();
            g->strut(5);
            g->text("acc", 0x50CFE5);
            loopi(lastpreviewdata.nplayers) { if(isingroup(name, i)) g->textf("%d%%", 0xFFFFFF, NULL, NULL, lastpreviewdata.players[i].acc); }
            g->poplist();
        }

        if(serverpreviewcn)
        {
            g->pushlist();
            g->strut(3);
            g->text("cn", 0x50CFE5);
            loopi(lastpreviewdata.nplayers) { if(isingroup(name, i)) g->textf("%d", 0xFFFFFF, NULL, NULL, lastpreviewdata.players[i].cn); }
            g->poplist();
        }

        if(serverpreviewping)
        {
            g->pushlist();
            g->strut(6);
            g->text("ping", 0x50CFE5);
            loopi(lastpreviewdata.nplayers) { if(isingroup(name, i)) g->textf("%d", 0xFFFFFF, NULL, NULL, lastpreviewdata.players[i].ping); }
            g->poplist();
        }

        if(serverpreviewip)
        {
            g->pushlist();
            g->strut(11);
            g->text("ip", 0x50CFE5);
            loopi(lastpreviewdata.nplayers) { if(isingroup(name, i)) g->textf("%s", 0xFFFFFF, NULL, NULL, GeoIP_num_to_addr(endianswap(lastpreviewdata.players[i].ip))); }
            g->poplist();
        }
    }

    int allpreviewspecs()
    {
        int tmp = 0;
        loopi(lastpreviewdata.nplayers)
            if(isspec(i)) tmp++;
        return tmp;
    }

    VARP(serverpreviewspecshortlist, 0, 0, 1);
    VARP(serverpreviewspeccn, 0, 1, 1);
    VARP(serverpreviewspecping, 0, 1, 1);
    VARP(serverpreviewspecip, 0, 0, 1);

    void drawspecs(g3d_gui *g)
    {
        g->pushlist();
        g->strut(15);
        g->text("spectator", 0x50CFE5);
        loopi(lastpreviewdata.nplayers)
        {
            if(isspec(i))
            {
                int status = 0xFFFFFF;
                bool isignored = ipignore::isignored(lastpreviewdata.players[i].ip);
                if(lastpreviewdata.players[i].privilege) status = guiprivcolor(lastpreviewdata.players[i].privilege);
                if(isignored)
                {
                    g->pushlist();
                    g->background(0xC4C420);
                }
                g->text(lastpreviewdata.players[i].name, status);
                if(isignored) g->poplist();
            }
        }
        g->poplist();

        if(serverpreviewspeccn)
        {
            g->pushlist();
            g->strut(3);
            g->text("cn", 0x50CFE5);
            loopi(lastpreviewdata.nplayers) { if(isspec(i)) g->textf("%d", 0xFFFFFF, NULL, NULL, lastpreviewdata.players[i].cn); }
            g->poplist();
        }

        if(serverpreviewspecping)
        {
            g->pushlist();
            g->strut(6);
            g->text("ping", 0x50CFE5);
            loopi(lastpreviewdata.nplayers) { if(isspec(i)) g->textf("%d", 0xFFFFFF, NULL, NULL, lastpreviewdata.players[i].ping); }
            g->poplist();
        }

        if(serverpreviewspecip)
        {
            g->pushlist();
            g->strut(11);
            g->text("ip", 0x50CFE5);
            loopi(lastpreviewdata.nplayers) { if(isspec(i)) g->textf("%s", 0xFFFFFF, NULL, NULL, GeoIP_num_to_addr(endianswap(lastpreviewdata.players[i].ip))); }
            g->poplist();
        }
    }

    void showserverpreview(g3d_gui *g)
    {
        g->allowautotab(false);
        if(lastpreviewdata.hasserverdata)
        {
            hasfreeslot = !(lastpreviewdata.sdata.nclients >= lastpreviewdata.sdata.maxclients);
            string hostname;
            if(enet_address_get_host_ip(&lastpreviewdata.servaddress, hostname, sizeof(hostname)) >= 0)
            {
                // TODO: map image somewhere top-left
                // g->image(textureload(tempformatstring("packages/base/%s.jpg", lastpreviewdata.sdata.servname)), 2, false);
                g->pushlist();
                g->spring();
                g->titlef("%s", 0xFFFFFF, NULL, NULL, lastpreviewdata.sdata.description);
                if(lastpreviewdata.sdata.nclients>=lastpreviewdata.sdata.maxclients)
                {
                    g->titlef("  \f3%d/%d  \f7%d", 0xFFFFFF, NULL, NULL,
                              lastpreviewdata.sdata.nclients,
                              lastpreviewdata.sdata.maxclients, lastpreviewdata.sdata.ping);
                }
                else g->titlef("  %d/%d  %d", 0xFFFFFF, NULL, NULL,
                              lastpreviewdata.sdata.nclients,
                              lastpreviewdata.sdata.maxclients, lastpreviewdata.sdata.ping);
                g->spring();
                g->poplist();
                g->pushlist();
                g->spring();
                g->textf("%s", 0x50CFE5, NULL, NULL, server::prettymodename(lastpreviewdata.sdata.mode));
                g->separator();
                g->textf("%s", 0x50CFE5, NULL, NULL, lastpreviewdata.sdata.servname);
                if(lastpreviewdata.sdata.gamespeed != 100)
                {
                    g->separator();
                    g->textf("%d.%02dx", 0x50CFE5, NULL, NULL, lastpreviewdata.sdata.gamespeed/100, lastpreviewdata.sdata.gamespeed%100);
                }
                g->separator();
                int secs = lastpreviewdata.sdata.timelimit%60, mins = lastpreviewdata.sdata.timelimit/60;
                g->pushlist();
                g->strut(mins >= 10 ? 4.5f : 3.5f);
                g->textf("%d:%02d", 0x50CFE5, NULL, NULL, mins, secs);
                g->poplist();
                if(lastpreviewdata.sdata.gamepaused)
                {
                    g->separator();
                    g->text("paused", 0x50CFE5);
                }
                g->spring();
                g->poplist();
            }
        }

        if(lastpreviewdata.hasplayerdata && lastpreviewdata.nplayers)
        {
            g->separator();
            if(lastpreviewdata.tinfo.notteammode)
            {
                if(hasplayers(NULL))
                {
                    g->pushlist();
                    drawgroup(g, NULL);
                    g->poplist();
                }
            }
            else
            {
                int groups = lastpreviewdata.tinfo.nteams;
                int validgroups = 0;
                loopi(groups) { if(hasplayers(lastpreviewdata.tinfo.teams[i].teamname)) validgroups++; }
                int k = 0;
                loopi(groups)
                {
                    if(!hasplayers(lastpreviewdata.tinfo.teams[i].teamname)) continue;
                    if((k%2)==0) g->pushlist();
                    g->pushlist();
                    if(lastpreviewdata.tinfo.teams[i].score>=10000) g->titlef("%s: WIN", 0xFFFFFF, NULL, NULL, lastpreviewdata.tinfo.teams[i].teamname);
                    else g->titlef("%s: %d", 0xFFFFFF, NULL, NULL, lastpreviewdata.tinfo.teams[i].teamname, lastpreviewdata.tinfo.teams[i].score);

                    g->separator();

                    g->pushlist();
                    drawgroup(g, lastpreviewdata.tinfo.teams[i].teamname);
                    g->poplist();
                    g->poplist();
                    if(k+1<validgroups && (k+1)%2) g->space(5);
                    else
                    {
                        g->poplist();
                        if(k+1 != validgroups) g->space(1);
                    }
                    k++;
                }
            }
            if(hasspecs())
            {
                g->space(1);
                g->pushlist();
                drawspecs(g);
                g->poplist();
            }
        }
        g->separator();
        g->pushlist();
        g->spring();
        if(g->button("Back", 0xFFFFFF, "exit")&G3D_UP)
        {
            g->poplist();
            lastpreviewdata.reset();
            g->allowautotab(true);
            selectedserver = NULL;
            cleargui(1);
        }
        g->separator();
        if(hasfreeslot)
        {
            if(g->button("Connect", 0xFFFFFF, "action")&G3D_UP)
            {
                g->poplist();
                g->end();
                lastpreviewdata.reset();
                g->allowautotab(true);
                connectselected();
            }
        }
        else if(!waitforfreeslot)
        {
            if(g->button("Wait for a free slot", 0xFFFFFF, "action")&G3D_UP) waitforfreeslot = true;
        }
        else g->text("Waiting for a free slot..", 0xFFFFFF);
        g->spring();
        g->poplist();
        g->allowautotab(true);

        if(waitforfreeslot && hasfreeslot)
        {
            waitforfreeslot = false;
            g->poplist();
            g->end();
            lastpreviewdata.reset();
            g->allowautotab(true);
            connectselected();
        }
        else if(hasfreeslot) waitforfreeslot = false;
    }

    void check()
    {
        const ENetAddress *paddress = connectedpeer();
        if(!paddress) return;
        process();
        loopv(clients)
        {
            fpsent * d = clients[i];
            if(!d) continue;
            if(d->extdata.needretry())
            {
                d->extdata.addattempt();
                requestplayer(d->clientnum);
            }
        }
        if(player1->extdata.needretry())
        {
            player1->extdata.addattempt();
            requestplayer(player1->clientnum);
        }
    }

    ENetSocket extinfosock2 = ENET_SOCKET_NULL;

    ENetSocket getextsock2()
    {
        if(extinfosock2 != ENET_SOCKET_NULL) return extinfosock2;
        extinfosock2 = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        enet_socket_set_option(extinfosock2, ENET_SOCKOPT_NONBLOCK, 1);
        enet_socket_set_option(extinfosock2, ENET_SOCKOPT_BROADCAST, 1);
        return extinfosock2;
    }

    void requestgameinfo(ENetAddress address)
    {
        ENetSocket extsock = getextsock2();
        if(!extsock) return;
        ENetBuffer buf;
        uchar send[MAXTRANS];
        ucharbuf p(send, MAXTRANS);
        putint(p, 0);
        putint(p, EXT_PLAYERSTATS);
        putint(p, -1);
        buf.data = send;
        buf.dataLength = p.length();
        enet_socket_send(extsock, &address, &buf, 1);
    }

    #define DISCONNECTEDINTERVAL 10000

    void checkservergameinfo()
    {
        ENetSocket extsock = getextsock2();
        if(!extsock) return;
        enet_uint32 events = ENET_SOCKET_WAIT_RECEIVE;
        int s = 0;
        ENetBuffer buf;
        ENetAddress address;
        uchar data[MAXTRANS];
        buf.data = data;
        buf.dataLength = sizeof(data);
        while((s = enet_socket_wait(extsock, &events, 0)) >= 0 && events)
        {
            int len = enet_socket_receive(extsock, &address, &buf, 1);
            if(len <= 0) continue;
            ucharbuf p(data, len);
            struct extplayerdata extpdata;
            if(!extplayershelper(p, extpdata))
            {
                void *p = getservergameinfo(address);
                struct serverpreviewdata *pr;
                if(!p) pr = new serverpreviewdata;
                else pr = (struct serverpreviewdata *)p;
                pr->addplayer(extpdata);
                pr->checkdisconnected(DISCONNECTEDINTERVAL);
                saveservergameinfo(address, pr);
            }
        }
    }

    void cleangameinfo(void *p)
    {
        if(!p) return;
        serverpreviewdata *s = (serverpreviewdata *)p;
        delete s;
    }

    /**
     *  Players-GUi stuff
     */

    bool playersentrysort (const playersentry& p1, const playersentry& p2)
    {
        int r = strncmp(p1.pname, p2.pname, MAXEXTNAMELENGHT);
        if(r) return r < 0;
        return strncmp(p1.sdesc, p2.sdesc, MAXSERVSTRING) < 0;
    }

    static char prevhost[100];
    static int prevport;

    static void onconnectseq(g3d_gui *g, playersentry &e)
    {
        g->poplist();
        g->mergehits(false);
        g->poplist();
        g->allowautotab(true);

        if(showserverpreviews)
        {
            strncpy(prevhost, e.shost, 100);
            prevport = e.sport;
            setserverpreview(prevhost, prevport);
            execute("showgui serverextinfo");
        }
        else connectserver(e.shost, e.sport);
    }

    int autoupdateplayersearch = 1;
    static char savedfilter[MAXEXTNAMELENGHT];

    bool needsearch = false;
    void showplayersgui(g3d_gui *g)
    {
        if(autoupdateplayersearch) needsearch = true;
        vector<serverinfodata *> v = getservers();
        vector<playersentry> p0, pe;

        loopv(v)
        {
            serverinfodata *s = v[i];
            if(!s) continue;

            serverpreviewdata *p = static_cast<serverpreviewdata *>(s->gameinfo);
            if(!p || s->ping == serverinfodata::WAITING) continue;

            if(autoupdateplayersearch) p->checkdisconnected(DISCONNECTEDINTERVAL);
            loopj(p->nplayers)
            {
                int mode = 0, maxplayers = 0, icon = 0;
                if(s->attr.length() >= 4)
                {
                    maxplayers = s->attr[3];
                    icon = s->attr[4];
                }
                if(s->attr.length() >= 1) mode = s->attr[1];
                p0.add(playersentry(
                    p->players[j].name,
                    s->sdesc,
                    s->name,
                    s->ping,
                    s->numplayers,
                    maxplayers,
                    mode,
                    icon,
                    s->address.host,
                    s->port
                    )
                );
            }
        }

        g->allowautotab(false);
        g->pushlist();
        g->textf("Filter: ", 0xFFFFFF, NULL);
        const char *filter = g->field("", 0xFFFFFF, MAXEXTNAMELENGHT-1, 0, savedfilter);
        g->separator();
        if(g->button("Clans", 0xFFFFFF, NULL)&G3D_UP) filter = "clans";
        g->separator();
        if(g->button("Friends", 0xFFFFFF, NULL)&G3D_UP) filter = "friends";
        g->separator();
        if(g->button("auto-update", 0xFFFFFF, autoupdateplayersearch ? "radio_on" : "radio_off")&G3D_UP) autoupdateplayersearch = !autoupdateplayersearch;
        g->poplist();
        g->separator();

        if(filter) strncpy(savedfilter, filter, MAXEXTNAMELENGHT-1);
        if(strnlen(savedfilter, MAXEXTNAMELENGHT-1) > 0)
        {
            if(strstr(savedfilter, "clans") != NULL)
            {
                loopv(p0)
                {
                    loopvj(clantags)
                        if(strstr(p0[i].pname, clantags[j]->name) != NULL) pe.add(p0[i]);
                }
            }
            else if(strstr(savedfilter, "friends") != NULL)
            {
                loopv(p0)
                {
                    loopvj(friends)
                        if(strstr(p0[i].pname, friends[j]->name) != NULL) pe.add(p0[i]);
                }
            }
            else loopv(p0) if(strstr(p0[i].pname, savedfilter) != NULL) pe.add(p0[i]);
        }
        else pe = p0;
        pe.sort(playersentrysort);
        int len = pe.length(), k = 0, kt = 0, maxcount = 20;

        loopi(len/maxcount + 1)
        {
            if(i>0 && k<len)
            {
                g->tab();
                g->pushlist();
                g->textf("Filter: ", 0xFFFFFF, NULL);
                g->field("", 0xFFFFFF, MAXEXTNAMELENGHT-1, 0, savedfilter);
                g->spring();
                if(g->button("auto-update", 0xFFFFFF, autoupdateplayersearch ? "radio_on" : "radio_off")&G3D_UP) autoupdateplayersearch = !autoupdateplayersearch;
                g->poplist();
                g->separator();
            }

            g->pushlist();
            g->mergehits(true);
            g->pushlist();
            g->strut(20);
            g->text("name", 0x50CFE5);
            kt = k;
            loopj(maxcount)
            {
                if(kt>=len) g->buttonf(" ", 0xFFFFFF, NULL);
                else
                {
                    playersentry e = pe[kt];
                    if(g->buttonf("%s", 0xFFFFFF, NULL, NULL, e.pname)&G3D_UP)
                    {
                        onconnectseq(g, e);
                        return;
                    }
                    kt++;
                }
            }
            g->poplist();

            g->separator();

            g->pushlist();
            g->strut(20);
            g->text("server", 0x50CFE5);
            kt = k;
            loopj(maxcount)
            {
                if(kt>=len) break;
                playersentry e = pe[kt];
                if(g->buttonf("%.25s", 0xFFFFFF, NULL, NULL, e.sdesc)&G3D_UP)
                {
                    onconnectseq(g, e);
                    return;
                }
                kt++;
            }
            g->poplist();
            g->mergehits(false);
            g->poplist();
            k = kt;
        }
        g->allowautotab(true);
    }

    void update()
    {
        event::run(event::EXTINFO_UPDATE);
        checkseserverinfo();
        check();
        checkservergameinfo();
        if(needsearch)
        {
            forceinitservers();
            refreshservers();
            needsearch = false;
        }
    }
}

void splitlist(const char *s, vector<char *> &elems)
{
    const char *start = s, *end;
    while((end = strchr(start, ' '))!=NULL)
    {
        elems.add(newstring(start, end-start));
        start = end+1;
    }
    if (start[0]) elems.add(newstring(start, s+strlen(s)-start));
}

namespace whois
{
    VARP(enablewhois, 0, 1, 1);

    struct whoisent
    {
        uint ip;
        vector<char *> names;
    };

    vector<whoisent> whoisents;

    ICOMMAND(whoisentry, "sss", (const char *ip, const char *names), {
                if(!names) return;

                whoisent w;
                w.ip = endianswap(GeoIP_addr_to_num(ip));
                w.ip = w.ip&0xFFFFFF;
                splitlist(names, w.names);
                whoisents.add(w);
             });

    vector<char *> *playernames(int cn)
    {
        fpsent *d = getclient(cn);
        if(!d) return NULL;

        uint cur_ip = d->extdata.data.ip&0xFFFFFF;
        loopi(whoisents.length()) if(whoisents[i].ip == cur_ip) return &whoisents[i].names;

        return NULL;
    }

    void addwhoisentry(uint ip, const char *name)
    {
        if(!enablewhois || !ip) return;

        uint cur_ip = ip&0xFFFFFF;
        bool found = false;

        loopi(whoisents.length()) if(whoisents[i].ip == cur_ip)
        {
            loopj(whoisents[i].names.length())
            {
                if(!strcmp(whoisents[i].names[j], name))
                {
                    whoisents[i].names.remove(j);
                    break;
                }
            }

            whoisents[i].names.insert(0, newstring(name));
            while(whoisents[i].names.length() > 15) delete[] whoisents[i].names.pop();
            found = true;
        }

        if(!found)
        {
            whoisent &w = whoisents.add();
            w.ip = cur_ip;
            w.names.add(newstring(name));
        }
    }

    void addplayerwhois(int cn)
    {
        fpsent *d = getclient(cn);
        if(!d || !d->extdata.data.ip) return;

        addwhoisentry(d->extdata.data.ip, d->name);
    }

    void whoisip(uint ip, const char *name)
    {
        if(!enablewhois || !ip) return;

        uint cur_ip = ip&0xFFFFFF;
        loopi(whoisents.length())
        {
            if(whoisents[i].ip == cur_ip)
            {
                defformatstring(line)("\f1%s \f0used then names: \f3", name);
                loopvj(whoisents[i].names)
                {
                    concatstring(line, whoisents[i].names[j]);
                    concatstring(line, " ");
                }
                conoutf(line);
            }
        }
    }

    void whois(const int cn)
    {
        fpsent *d = getclient(cn);
        if(!enablewhois || !d || !d->extdata.data.ip) return;

        uint cur_ip = d->extdata.data.ip&0xFFFFFF;
        loopi(whoisents.length())
        {
            if(whoisents[i].ip == cur_ip)
            {
                defformatstring(line)("\f1%s \f0used the names: \f3", d->name);
                loopvj(whoisents[i].names)
                {
                    concatstring(line, whoisents[i].names[j]);
                    concatstring(line, " ");
                }
                conoutf(line);
            }
        }
    }

    const char *getwhoisnames(const int cn)
    {
        const char *buf = NULL;

        fpsent *d = getclient(cn);

        buf = tempformatstring("%s %d %d", buf, cn, d->extdata.data.ip);

        uint cur_ip = d->extdata.data.ip&0xFFFFFF;
        loopi(whoisents.length())
        {
            if(whoisents[i].ip == cur_ip)
            {
                loopvj(whoisents[i].names)
                {
                    buf = tempformatstring("%s, %s", buf, whoisents[i].names[j]);
                }
            }
        }
        return buf;
    }

    void whoisname(const char *name, int exact)
    {
        if(!enablewhois || !name || !name[0]) return;

        bool noonefound = true;
        conoutf("\f0Everyone who used the name \f1%s \f0:", name);
        loopi(whoisents.length())
        {
            bool found = false;
            string cname, bname;

            if(!exact)
            {
                strcpy(cname, name);
                strlwr(cname);
            }

            loopvj(whoisents[i].names)
            {
                if(!exact)
                {
                    strcpy(bname, whoisents[i].names[j]);
                    strlwr(bname);
                    if(strstr(bname, cname)) { noonefound = false; found = true; }
                }
                else if(strcasecmp(whoisents[i].names[j], name) == 0) { noonefound = false; found = true; }
            }

            if(found)
            {
                #ifndef QUED32
                defformatstring(line)("\f0%d.%d.%d.* (%s): \f3", whoisents[i].ip&0xFF, (whoisents[i].ip&0xFF00)>>8, (whoisents[i].ip&0xFF0000)>>16, GeoIP_country_name_by_ipnum());
                #else
                defformatstring(line)("\f0%d.%d.%d.* (%s): \f3", whoisents[i].ip&0xFF, (whoisents[i].ip&0xFF00)>>8, (whoisents[i].ip&0xFF0000)>>16, GeoIP_country_name_by_ipnum(geoip, endianswap(whoisents[i].ip)));
                #endif

                loopvj(whoisents[i].names)
                {
                    concatstring(line, whoisents[i].names[j]);
                    concatstring(line, " ");
                }
                conoutf("%s", line);
            }
        }
        if(noonefound) conoutf("no one :(");
    }

    ICOMMAND(whois, "i", (const int *cn), whois(*cn));
    ICOMMAND(whoisname, "si", (const char *name, const int *exact), whoisname(name, *exact));

    void loadwhoisdb() { execfile("whois.db", false); }
    void writewhoisdb()
    {
        stream *f = openutf8file(path("whois.db", true), "w");
        loopv(whoisents)
        {
            f->printf("whoisentry %s ", GeoIP_num_to_addr(endianswap(whoisents[i].ip)));
            char text[MAXTRANS];
            text[0] = '\0';
            loopj(whoisents[i].names.length())
            {
                concatstring(text, whoisents[i].names[j]);
                if(j != whoisents[i].names.length()-1) concatstring(text, " ");
            }
            f->putstring(escapestring(text));
            f->printf("\n");
            whoisents[i].names.deletearrays();
        }
        delete f;
        whoisents.setsize(0);
    }

    ICOMMAND(whereis, "i", (const int *cn),
    {
        fpsent *d = game::getclient(*cn);
        #ifndef QUED32
        if(d) conoutf("%s(%d) connected from %s", d->name, d->clientnum, GeoIP_country_name_by_ipnum());
        #else
        if(d) conoutf("%s(%d) connected from %s", d->name, d->clientnum, GeoIP_country_name_by_ipnum(geoip, endianswap(d->extdata.data.ip)));
        #endif
        else conoutf("\f3error: client \"cn %d\" not found!", *cn);
    });
    ICOMMAND(getclientip, "i", (const int *cn),
    {
        fpsent *d = game::getclient(*cn);
        if(d) result(newstring(d->getip()));
        else
        {
            if(debugquality) conoutf("\f3error: client \"cn %d\" not found!", *cn);
            result("WRONG_CN");
        }
    });
}
