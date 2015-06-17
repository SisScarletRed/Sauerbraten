#include "demo.h"

namespace game
{
    extern int gamemode,
               gamespeed,
               mastermode;
    extern void concatgamedesc(char *name, size_t maxlen = MAXSTRLEN);
    extern const char *demospath(char *n);
}

extern const char *gettimestr(const char *format, bool forcelowercase = false);
extern void cutextension(char *str, char *ext);
VARP(debugdemo, 0, 0, 1);

namespace demo
{
    static stream *demostream = NULL;
    static string name;
    static int starttime = 0;

    static uchar somespace[MAXTRANS];
    static ucharbuf p(somespace, MAXTRANS);

    bool isrecording = false;
    SVARP(demorecordername, "Quality");

    const char *keepdemogui_char =
        "newgui keepdemogui [\n"
            "guititle \"Keep demo?\"\n"
            "guialign 0 [\n"
                "guilist [\n"
                    "guibar\n"
                    "guitext \"Automatic client demo recording is ^f0active.\"\n"
                    "guitext \"Would you like to ^fs^f0keep^fr the last recorded demo?\"\n"
                    "guistrut\n"
                    "guilist [\n"
                        "guispring\n"
                        "guibutton \"^fs^f0Yes^fr (default)\" [keepdemo 1]\n"
                        "guispring\n"
                        "guibutton \"^fs^f3No^fr\" [keepdemo 0]\n"
                        "guispring\n"
                    "]\n"
                    "guibar\n"
                "]\n"
            "]\n"
        "] \"Client Demo\"\n";

    static void writedemo(int chan, void *data, int len)
    {
        if(!demostream) return;

        int stamp[3] = { lastmillis-starttime, chan, len };
        lilswap(stamp, 3);
        demostream->write(stamp, sizeof(stamp));
        demostream->write(data, len);
    }

    void _putdemoinfo(int type, const char *_text)
    {
        if(!demostream) return;

        char text[MAXTRANS];
        strncpy(text, _text, MAXTRANS-1);

        p.len = 0;
        putint(p, type);
        sendstring(text, p);
        packet(1, p);
    }

    void _putdemoclientip(int type, int cn, uint ip)
    {
        if(!demostream) return;

        p.len = 0;
        putint(p, type);
        putint(p, cn);
        p.put((uchar *)&ip, 3);
        packet(1, p);
    }

    void setup(const char *name_)
    {
        using game::gamemode;
        using game::players;
        using entities::ents;

        if(!m_mp(gamemode) || game::demoplayback || !game::connected) return;
        if(demostream) stop();

        string defname = { '\0' };
        game::concatgamedesc(defname);
        formatstring(name)("%s.dmo", name_ && name_[0] ? name_ : defname);
        demostream = opengzfile(path(tempformatstring("demos/%s", name)), "w+b");
        if(!demostream) return;

        starttime = totalmillis;
        demoheader hdr;
        memcpy(hdr.magic, QUALITY_DEMO_MAGIC, sizeof(hdr.magic));
        hdr.version = QUALITY_DEMO_VERSION;
        hdr.protocol = QUALITY_PROTOCOL_VERSION;
        lilswap(&hdr.version, 2);
        demostream->write(&hdr, sizeof(demoheader));

        p.len = 0;
        putint(p, N_WELCOME);
        putint(p, N_MAPCHANGE);
        sendstring(game::getclientmap(), p);
        putint(p, gamemode);
        putint(p, 0);
        putint(p, N_ITEMLIST);
        loopv(ents)
            if(ents[i]->type>=I_SHELLS && ents[i]->type<=I_QUAD && (!m_noammo || ents[i]->type<I_SHELLS || ents[i]->type>I_CARTRIDGES) && ents[i]->spawned())
            {
                putint(p, i);
                putint(p, ents[i]->type);
            }
        putint(p, -1);
        putint(p, N_TIMEUP);
        putint(p, lastmillis < game::maplimit && !game::intermission ? max((game::maplimit - lastmillis)/1000, 1) : 0);

        bool hasmaster = false;
        if(game::mastermode != MM_OPEN)
        {
            putint(p, N_CURRENTMASTER);
            putint(p, game::mastermode);
            hasmaster = true;
        }

        loopv(players) if(players[i]->privilege >= PRIV_MASTER)
        {
            if(!hasmaster)
            {
                putint(p, N_CURRENTMASTER);
                putint(p, game::mastermode);
                hasmaster = true;
            }
            putint(p, players[i]->clientnum);
            putint(p, players[i]->privilege);
        }

        if(hasmaster) putint(p, -1);

        if(game::ispaused())
        {
            putint(p, N_PAUSEGAME);
            putint(p, 1);
        }

        if(game::gamespeed != 100)
        {
            putint(p, N_GAMESPEED);
            putint(p, game::gamespeed);
            putint(p, -1);
        }
        if(m_teammode)
        {
            putint(p, N_TEAMINFO);
            enumerates(game::teaminfos, teaminfo, t, if(t.frags) { sendstring(t.team, p); putint(p, t.frags); });
            sendstring("", p);
        }

        putint(p, N_RESUME);

        loopv(players)
        {
            fpsent* d = players[i];
            putint(p, d->clientnum);
            putint(p, d->state);
            putint(p, d->frags);
            putint(p, d->flags);
            putint(p, d->quadmillis);
            putint(p, d->lifesequence);
            putint(p, d->health);
            putint(p, d->maxhealth);
            putint(p, d->armour);
            putint(p, d->armourtype);
            putint(p, d->gunselect);
            loopi(GUN_PISTOL-GUN_SG+1) putint(p, d->ammo[GUN_SG+i]);
        }

        putint(p, -1);

        loopv(players)
        {
            fpsent* d = players[i];
            if(d->aitype != AI_NONE)
            {
                putint(p, N_INITAI);
                putint(p, d->clientnum);
                putint(p, d->ownernum);
                putint(p, d->aitype);
                putint(p, d->skill);
                putint(p, d->playermodel);
                sendstring(d->name, p);
                sendstring(d->team, p);
            }
            else
            {
                putint(p, N_INITCLIENT);
                putint(p, d->clientnum);
                sendstring(d->name, p);
                sendstring(d->team, p);
                putint(p, d->playermodel);
            }
        }
        if(m_ctf) ctfinit(p);
        if(m_capture) captureinit(p);
        if(m_collect) collectinit(p);

        putint(p, N_SERVMSG);
        string info;
        if(name_ && name_[0]) formatstring(info)("original filename: %s.dmo (canonical: %s.dmo)", name, defname);
        else formatstring(info)("original filename: %s", name);
        sendstring(info, p);
        packet(1, p);

        _putdemoinfo(N_CDEMO_DATERECORDED, gettimestr("%d %b %y"));
        _putdemoinfo(N_CDEMO_CLIENTRECORDED, demorecordername);

        ::executestr(keepdemogui_char);
        conoutf("\f3recording client demo");
        isrecording = true;
    }

    ENetPacket *packet(int chan, const ENetPacket *p)
    {
        if(demostream) writedemo(chan, p->data, p->dataLength);
        return const_cast<ENetPacket *> (p);
    }

    void packet(int chan, const ucharbuf &p)
    {
        if(!demostream) return;
        writedemo(chan, p.buf, p.len);
    }

    static bool skiptextonce = false;
    void addmsghook(int type, int cn, const ucharbuf &addmsgp)
    {
        if(!demostream) return;

        switch(type)
        {
            case N_TEXT:
                if(skiptextonce)
                {
                    skiptextonce = false;
                    return;
                }
            case N_GUNSELECT:
            case N_SWITCHTEAM:
            case N_SWITCHMODEL:
            case N_CLIENTPING:
            case N_SOUND:
            case N_TAUNT:
            case N_EDITMODE:
            case N_EDITENT:
            case N_EDITF:
            case N_EDITT:
            case N_EDITM:
            case N_FLIP:
            case N_COPY:
            case N_PASTE:
            case N_ROTATE:
            case N_REPLACE:
            case N_DELCUBE:
            case N_REMIP:
            case N_EDITVAR:
                p.len = 0;
                putint(p, N_CLIENT);
                putint(p, cn == -1 ? int(game::player1->clientnum) : cn);
                putint(p, addmsgp.len);
                p.put(addmsgp.buf, addmsgp.len);
                packet(1, p);
            default:
                return;
        }
    }

    void sayteam(const char *msg)
    {
        if(!demostream) return;

        p.len = 0;
        putint(p, game::player1->clientnum);
        sendstring(msg, p);
        packet(1, p);
    }

    void clipboard(int plainlen, int packlen, const uchar *data)
    {
        if(!demostream) return;

        packetbuf q(32 + packlen);
        putint(q, N_CLIPBOARD);
        putint(q, game::player1->clientnum);
        putint(q, plainlen);
        putint(q, packlen);
        q.put(data, packlen);
        packet(1, q);
    }

    void explodefx(int cn, int gun, int id)
    {
        if(!demostream) return;

        p.len = 0;
        putint(p, N_EXPLODEFX);
        putint(p, cn);
        putint(p, gun);
        putint(p, id);
        packet(1, p);
    }

    void shotfx(int cn, int gun, int id, const vec& from, const vec& to)
    {
        if(!demostream) return;

        p.len = 0;
        putint(p, N_SHOTFX);
        putint(p, cn);
        putint(p, gun);
        putint(p, id);
        loopi(3) putint(p, int(from[i]*DMF));
        loopi(3) putint(p, int(to[i]*DMF));
        packet(1, p);
    }

    VARP(showkeepdemogui, 0, 1, 1);
    VARP(showkeepdemoguialways, 0, 1, 1);

    string curname;
    void stop(bool askkeep)
    {
        if(!demostream) return;

        DELETEP(demostream);
        copystring(curname, tempformatstring("demos/%s", name));
        if((demoauto && showkeepdemogui && askkeep) || showkeepdemoguialways) showgui("keepdemogui");
        else conoutf("\f0recorded client demo (%s)", name);
        isrecording = false;
    }

    VARP(demoauto, 0, 0, 1);
    VARP(demoautonotlocal, 0, 1, 1);
    COMMANDN(demostart, setup, "s");
    ICOMMAND(demostop, "i", (int *keep), stop(*keep));
    ICOMMAND(say_nodemo, "s", (char *s), { skiptextonce = true; game::toserver(s); });

    void keepdemo(int *keep)
    {
        string nil;
        copystring(nil, "null");
        if(*keep == 0)
        {
            if(strcmp(curname, nil) != 0)
            {
                int deleted;
                string tmp;
                copystring(tmp, homedir);
                concatstring(tmp, curname);
                deleted = remove(tmp);
                if(deleted != 0) conoutf("\f3Error: could not delete demo %s", curname);
                else
                {
                    conoutf("\f0Successfully deleted auto-recorded demo %s", curname);
                    if(!game::connected) copystring(curname, nil);
                }
            }
            else
            {
                conoutf("\f3Error: empty demo name. no demo recorded or deleted!");
                if(!game::connected) copystring(curname, nil);
            }
        }
        else
        {
            conoutf("\f0kept recorded client demo (%s)", curname);
            if(!game::connected) copystring(curname, nil);
        }
    }
    COMMAND(keepdemo, "i");

    namespace preview
    {
        FVAR(demostartmin, 0, 0, 15);
        VARP(demopreviewdebug, 0, 0, 1);

        bool hastoupdate = false;
        int sortterm = 0;
        bool upsidedown;

        struct demoinfo
        {
            string map,
                   pathname,
                   gametype,
                   infoline,
                   date,
                   client;
            int mode,
                mastermode;
            stream *file;
            struct happening
            {
                int time,
                    cn,
                    type;
                char *text;
            };
            int nextreading;

            vector<happening *> events;
            vector<extteam *> teams;
            vector<extplayer *> spectator,
                                clients;

            demoinfo()
            {
                file = NULL;
                nextreading = map[0] = pathname[0] = gametype[0] = infoline[0] = 0;
                copystring(date, "-");
                copystring(client, "-");
            }
            ~demoinfo()
            {
                reset();
                DELETEP(file);
            }

            void reset()
            {
                map[0] = 0;
                mode = -1;

                loopv(events) DELETEP(events[i]);

                loopv(teams) DELETEP(teams[i]);
                teams.setsize(0);

                loopv(clients) DELETEP(clients[i]);
                clients.setsize(0);

                loopv(spectator) DELETEP(spectator[i]);
                spectator.setsize(0);
            }

            extplayer *getclient(int cn)
            {
                return clients.inrange(cn) ? clients[cn] : NULL;
            }

            extplayer *newclient(int cn)
            {
                while(cn >= clients.length()) clients.add(NULL);

                if(!clients[cn])
                {
                    extplayer *d = new extplayer;
                    d->cn = cn;
                    clients[cn] = d;
                }

                return clients[cn];
            }

            extteam *newteam(char *team, int j, bool newone = true)
            {
                if(j < 0 && (!team || !team[0])) return NULL;

                extteam *t = 0;

                if(!t) if(j >= 0 && teams.inrange(j)) t = teams[j];
                if(!t) loopv(teams) if(teams[i] && teams[i]->name[0] &&  team && team[0] && !strcmp(teams[i]->name, team))
                    t = teams[i];
                if(!t && newone)
                {
                    if(!team || !team[0]) t = teams.add(new extteam(newstring("good")));
                    else t = teams.add(new extteam(team));
                }

                return t;
            }

            void setteamfrags(char *team, int frags)
            {
                extteam *t = newteam(team, -1);
                if(!t) return;

                t->frags = frags;
            }

            void assignteams();
            bool isclanwar();
            bool isduel();
            void analyze();

            void addhappening(int type, char *text, int cn)
            {
                happening *d = new happening;

                d->time = nextreading;
                d->cn = cn;
                d->text = new char[strlen(text)+1];
                d->type = type;
                strcpy(d->text, text);
                events.add(d);
            }

            void load(char *demoname);
            void endreading();
            bool setupreading(char *filename);
            void parsestate(extplayer *d, ucharbuf &p, bool resume = false);
            void parsemsgs(int cn, extplayer *d, ucharbuf &p);

            static bool compare(demoinfo *a, demoinfo *b)
            {
                int val = -1;
                switch(sortterm)
                {
                    case SORT_FILE:
                        break;
                    case SORT_TYPE:
                        if(a->gametype[0] && b->gametype[0])
                        {
                            if(strcmp(a->gametype, b->gametype)>0) val = 1;
                            if(strcmp(a->gametype, b->gametype)<0) val = 0;
                        }
                        else
                        {
                            if(a->gametype[0] && !b->gametype[0]) val = 1;
                            if(!a->gametype[0] && b->gametype[0]) val = 0;
                        }
                        break;
                    case SORT_INFO:
                        if(a->infoline[0] && b->infoline[0])
                        {
                            if(strcmp(a->infoline, b->infoline)>0) val = 1;
                            if(strcmp(a->infoline, b->infoline)<0) val = 0;
                        }
                        else
                        {
                            if(a->infoline[0] && !b->infoline[0]) val = 1;
                            if(!a->infoline[0] && b->infoline[0]) val = 0;
                        }
                        break;
                    case SORT_MODE:
                        if(a->mode < b->mode) val = 1;
                        if(a->mode > b->mode) val = 0;
                        break;
                    case SORT_MAP:
                        if(a->map[0] && b->map[0])
                        {
                            if(strcmp(a->map, b->map)>0) val = 1;
                            if(strcmp(a->map, b->map)<0) val = 0;
                        }
                        else
                        {
                            if(a->map[0] && !b->map[0]) val = 1;
                            if(!a->map[0] && b->map[0]) val = 0;
                        }
                        break;
                }

                if(val >= 0) return upsidedown ? val == 0 : val != 0;

                if(a->pathname[0] && !b->pathname[0]) return upsidedown ? false : true;
                if(!a->pathname[0] && b->pathname[0]) return upsidedown ? true : false;

                if(a->pathname[0] && b->pathname[0])
                {
                    if(strcmp(a->pathname, b->pathname)>0) return upsidedown ? false : true;
                    if(strcmp(a->pathname, b->pathname)<0) return upsidedown ? true : false;
                }

                return upsidedown ? true : false;
            }
        };
        vector<demoinfo *> demoinfos;

        void demoinfo::parsestate(extplayer *d, ucharbuf &p, bool resume)
        {
            if(!d) { static extplayer dummy; d = &dummy; }
            if(resume)
            {
                d->state = getint(p);
                d->frags = getint(p);
                d->flags = getint(p);
                getint(p);
            }
            loopk(5) getint(p);

            int gun = getint(p);
            d->gunselect = clamp(gun, int(GUN_FIST), int(GUN_PISTOL));
            loopi(GUN_PISTOL-GUN_SG+1) getint(p);
        }

        void demoinfo::parsemsgs(int cn, extplayer *d, ucharbuf &p)
        {
            static char text[MAXTRANS];
            int type;

            while(p.remaining())
            {
                switch(type = getint(p))
                {
                    case N_WELCOME:
                    case N_DEMOPACKET:
                    case N_TAUNT:
                    case N_REMIP:
                        break;
                    case N_SERVINFO:
                    {
                        getint(p);
                        getint(p);
                        getint(p);
                        getint(p);
                        getstring(text, p, sizeof(text));
                        getstring(text, p, sizeof(text));
                        break;
                    }
                    case N_PAUSEGAME:
                    {
                        getint(p);
                        int cn = getint(p);
                        getclient(cn);
                        break;
                    }

                    case N_GAMESPEED:
                        getint(p);
                        getint(p);
                        break;

                    case N_CLIENT:
                    {
                        int cn = getint(p), len = getuint(p);
                        ucharbuf q = p.subbuf(len);
                        parsemsgs(cn, getclient(cn), q);
                        break;
                    }
                    case N_SOUND:
                        if(d) getint(p);
                        break;

                    case N_TEXT:
                    {
                        if(!d) return;
                        getstring(text, p);
                        filtertext(text, text);
                        addhappening(N_TEXT, text, d->cn);
                        break;
                    }

                    case N_SAYTEAM:
                    {
                        getint(p);
                        getstring(text, p);
                        break;
                    }

                    case N_MAPCHANGE:
                    {
                        getstring(text, p);
                        filtertext(text, text, false);
                        copystring(map, text);
                        mode = getint(p);
                        getint(p);
                        break;
                    }

                    case N_FORCEDEATH:
                    {
                        int cn = getint(p);
                        newclient(cn);
                        break;
                    }

                    case N_ITEMLIST:
                        while(getint(p)>=0 && !p.overread())
                        getint(p);
                        break;

                    case N_INITCLIENT:
                    {
                        int cn = getint(p);
                        extplayer *d = newclient(cn);
                        if(!d)
                        {
                            getstring(text, p);
                            getstring(text, p);
                            getint(p);
                            break;
                        }
                        getstring(text, p);
                        filtertext(text, text, false, MAXNAMELEN);
                        if(!text[0]) copystring(text, "unnamed");
                        copystring(d->name, text, MAXNAMELEN+1);
                        getstring(text, p);
                        filtertext(text, text, false, MAXTEAMLEN);
                        if(!text[0]) copystring(text, "good");
                        copystring(d->team, text, MAXNAMELEN+1);
                        d->playermodel = getint(p);
                        break;
                    }

                    case N_SWITCHNAME:
                        getstring(text, p);
                        if(d)
                        {
                            filtertext(text, text, false, MAXNAMELEN);
                            if(!text[0]) copystring(text, "unnamed");
                            if(strcmp(text, d->name)) copystring(d->name, text, MAXNAMELEN+1);
                        }
                        break;

                    case N_SWITCHMODEL:
                    {
                        int model = getint(p);
                        if(d)	d->playermodel = model;
                        break;
                    }

                    case N_CDIS:
                        getint(p);
                        break;

                    case N_SPAWN:
                    {
                        parsestate(d, p);
                        break;
                    }

                    case N_SPAWNSTATE:
                    {
                        int scn = getint(p);
                        extplayer *s = getclient(scn);
                        parsestate(s, p);
                        break;
                    }

                    case N_SHOTFX:
                    {
                        int scn = getint(p), gun = getint(p); getint(p);
                        loopk(6) getint(p);
                        extplayer *s = getclient(scn);
                        if(!s) break;
                        s->gunselect = clamp(gun, (int)GUN_FIST, (int)GUN_PISTOL);
                        break;
                    }

                    case N_EXPLODEFX:
                    {
                        getint(p); getint(p); getint(p);
                        break;
                    }

                    case N_DAMAGE:
                    {
                        loopk(5) getint(p);
                        break;
                    }

                    case N_HITPUSH:
                    {
                        loopk(6) getint(p) ;
                        break;
                    }

                    case N_DIED:
                    {
                        int vcn = getint(p), acn = getint(p), frags = getint(p), tfrags = getint(p);
                        extplayer *victim = getclient(vcn),	*actor = getclient(acn);
                        if(!actor) break;
                        actor->frags = frags;
                        if(m_check(mode, M_TEAM)) setteamfrags(actor->team, tfrags);
                        if(!victim) break;
                        victim->deaths++;
                        addhappening(N_DIED, victim->name, acn);
                        break;
                    }

                    case N_TEAMINFO:
                        for(;;)
                        {
                            getstring(text, p);
                            if(p.overread() || !text[0]) break;
                            int frags = getint(p);
                            if(p.overread()) break;
                            if(m_check(mode, M_TEAM)) setteamfrags(text, frags);
                        }
                        break;

                    case N_GUNSELECT:
                    {
                        if(!d) return;
                        int gun = getint(p);
                        d->gunselect = clamp(gun, int(GUN_FIST), int(GUN_PISTOL));
                        break;
                    }

                    case N_RESUME:
                    {
                        for(;;)
                        {
                            int cn = getint(p);
                            if(p.overread() || cn<0) break;
                            extplayer *d = newclient(cn);
                            parsestate(d, p, true);
                        }
                        break;
                    }

                    case N_ITEMSPAWN:
                        getint(p);
                        break;

                    case N_ITEMACC:
                        getint(p);
                        cn = getint(p);
                        break;

                    case N_CLIPBOARD:
                    {
                        getint(p); getint(p); getint(p);
                        break;
                    }

                    case N_EDITF: case N_EDITT: case N_EDITM: case N_FLIP: case N_COPY:
                    case N_PASTE: case N_ROTATE: case N_REPLACE: case N_DELCUBE:
                    {
                        if(!d) return;
                        getint(p); getint(p); getint(p);
                        getint(p); getint(p); getint(p);
                        getint(p); getint(p); getint(p);
                        getint(p); getint(p), getint(p);
                        getint(p);

                        switch(type)
                        {
                            case N_ROTATE:								getint(p); break;
                            case N_EDITF: case N_EDITT: case N_EDITM:	getint(p); getint(p); break;
                            case N_REPLACE:								getint(p); getint(p); getint(p); break;
                        }
                        break;
                    }

                    case N_EDITENT:
                    {
                        if(!d) return;
                        getint(p);
                        loopk(3) getint(p);
                        getint(p);
                        loopk(5) getint(p);
                        break;
                    }

                    case N_EDITVAR:
                    {
                        if(!d) return;
                        int type = getint(p);
                        getstring(text, p);
                        switch(type)
                        {
                            case ID_VAR:  getint(p); break;
                            case ID_FVAR: getfloat(p); break;
                            case ID_SVAR: getstring(text, p); break;
                        }
                        break;
                    }

                    case N_TIMEUP:
                    case N_PONG:
                        getint(p); break;

                    case N_CLIENTPING:
                        if(!d) return;
                        d->ping = getint(p);
                        break;

                    case N_SERVMSG:
                        getstring(text, p);
                        break;

                    case N_SENDDEMOLIST:
                    {
                        int demos = getint(p);
                        if(demos > 0)loopi(demos)
                        {
                            getstring(text, p);
                            if(p.overread()) break;
                        }
                        break;
                    }

                    case N_DEMOPLAYBACK:
                    {
                        getint(p);
                        getint(p);
                        break;
                    }

                    case N_CURRENTMASTER:
                    {
                        mastermode = getint(p);
                        int mn;
                        loopv(clients)	if(clients[i])	clients[i]->privilege = PRIV_NONE;

                        while((mn = getint(p))>=0 && !p.overread())
                        {
                            extplayer *m = newclient(mn);
                            int priv = getint(p);
                            if(m) m->privilege = priv;
                        }
                        break;
                    }

                    case N_MASTERMODE:	mastermode = getint(p);
                        break;

                    case N_EDITMODE: //todo
                    {
                        int val = getint(p);
                        if(!d) break;
                        if(val) d->state = CS_EDITING;
                        break;
                    }

                    case N_SPECTATOR:
                    {
                        int sn = getint(p), val = getint(p);
                        extplayer *s = newclient(sn);
                        if(!s) return;
                        if(val) s->state = CS_SPECTATOR;
                        else if(s->state==CS_SPECTATOR) s->state = CS_DEAD;
                        break;
                    }

                    case N_SETTEAM:
                    {
                        int wn = getint(p);
                        getstring(text, p);
                        getint(p);
                        extplayer *w = getclient(wn);
                        if(!w) return;
                        filtertext(w->team, text, false, MAXTEAMLEN);
                        break;
                    }

                    case N_BASEINFO:
                    {
                        getint(p);
                        getstring(text, p);
                        getstring(text, p);
                        getint(p); getint(p);
                        break;
                    }

                    case N_BASEREGEN:
                    {
                        getint(p);
                        getint(p);
                        getint(p);
                        getint(p);
                        getint(p);
                        break;
                    }

                    case N_BASES:
                    {
                        int numbases = getint(p);
                        loopi(numbases)
                        {
                            getint(p);
                            getstring(text, p);
                            getstring(text, p);
                            getint(p);
                            getint(p);
                        }
                        break;
                    }

                    case N_BASESCORE:
                    {
                        getint(p);
                        getstring(text, p);
                        getint(p);
                        break;
                    }

                    case N_REPAMMO:
                    {
                        getint(p); getint(p);
                        break;
                    }

                    case N_INITFLAGS:
                    {
                        loopk(2) getint(p);
                        int numflags = getint(p);
                        loopi(numflags)
                        {
                            getint(p);
                            getint(p);
                            int owner = getint(p);
                            getint(p);
                            int dropped = 0;

                            if(owner<0)
                            {
                                dropped = getint(p);
                                if(dropped) loopk(3) getint(p);
                            }
                            if(p.overread()) break;
                        }
                        break;
                    }

                    case N_DROPFLAG:
                    {
                        int ocn = getint(p); getint(p); getint(p);
                        vec droploc;
                        loopk(3) droploc[k] = getint(p)/DMF;
                        newclient(ocn);
                        break;
                    }

                    case N_SCOREFLAG:
                    {
                        int ocn = getint(p); getint(p); getint(p); getint(p); getint(p); getint(p);
                        int team = getint(p), score = getint(p), oflags = getint(p);
                        extplayer *o = newclient(ocn);
                        if(o) o->flags = oflags;
                        extteam *t = newteam(NULL, team-1, false);
                        if(!t) if(o) t = newteam(o->team, -1);
                        if(!t) return;
                        t->score = score;
                        addhappening(N_SCOREFLAG, t->name, ocn);
                        break;
                    }

                    case N_RETURNFLAG:
                    {
                        int ocn = getint(p);
                        getint(p);
                        getint(p);
                        newclient(ocn);
                        break;
                    }

                    case N_TAKEFLAG:
                    {
                        int ocn = getint(p);
                        getint(p);
                        getint(p);
                        newclient(ocn);
                        break;
                    }

                    case N_RESETFLAG:
                    {
                        loopk(5) getint(p);
                        break;
                    }

                    case N_INVISFLAG:
                    {
                        getint(p);
                        getint(p);
                        break;
                    }
                    case N_INITTOKENS:
                    {
                        loopk(2) getint(p);
                        int numtokens = getint(p);

                        loopi(numtokens)
                        {
                            getint(p);
                            getint(p);
                            getint(p);

                            loopk(3) getint(p);
                            if(p.overread()) break;
                        }

                        for(;;)
                        {
                            int cn = getint(p);
                            if(cn < 0) break;
                            getint(p);
                            if(p.overread()) break;
                            newclient(cn);
                        }
                        break;
                    }

                    case N_TAKETOKEN:
                    {
                        int ocn = getint(p); getint(p); getint(p);
                        newclient(ocn);
                        break;
                    }

                    case N_EXPIRETOKENS:
                        for(;;)
                        {
                            int id = getint(p);
                            if(p.overread() || id < 0) break;
                        }
                        break;

                    case N_DROPTOKENS:
                    {
                        int ocn = getint(p);
                        newclient(ocn);

                        loopk(3) getint(p);
                        for(int n = 0;; n++)
                        {
                            int id = getint(p);
                            if(id < 0) break;
                            getint(p); getint(p);
                            if(p.overread()) break;
                        }
                        break;
                    }

                    case N_STEALTOKENS:
                    {
                        int ocn = getint(p); getint(p); getint(p); getint(p); getint(p);
                        newclient(ocn);

                        loopk(3) getint(p);
                        for(int n = 0;; n++)
                        {
                            int id = getint(p);
                            if(id < 0) break;
                            getint(p);
                            if(p.overread()) break;
                        }
                        break;
                    }

                    case N_DEPOSITTOKENS:
                    {
                        int ocn = getint(p); getint(p); getint(p); getint(p); getint(p); getint(p);
                        newclient(ocn);
                        break;
                    }

                    case N_ANNOUNCE:
                        getint(p);
                        break;

                    case N_NEWMAP:
                    {
                        getint(p);
                        break;
                    }

                    case N_REQAUTH:
                    {
                        getstring(text, p);
                        break;
                    }

                    case N_AUTHCHAL:
                    {
                        getstring(text, p);
                        getint(p);
                        getstring(text, p);
                        break;
                    }

                    case N_INITAI:
                    {
                        int bn = getint(p); getint(p); getint(p); getint(p); getint(p);
                        extplayer *b = newclient(bn);
                        if(!b) break;
                        getstring(text, p);
                        filtertext(b->name, text, false, MAXNAMELEN);
                        getstring(text, p);
                        filtertext(b->team, text, false, MAXTEAMLEN);
                        break;
                    }

                    case N_SERVCMD:
                        getstring(text, p);
                        break;

                    // EXTENDED STUFF
                    case N_CDEMO_DATERECORDED:
                        getstring(text, p);
                        copystring(date, text);
                        break;

                    case N_CDEMO_CLIENTRECORDED:
                        getstring(text, p);
                        copystring(client, text);
                        break;

                    case N_CDEMO_CLIENTIP:
                    {
                        int cn = getint(p);
                        extplayer *e = newclient(cn);
                        getstring(text, p);
                        copystring(e->ip, text);
                        if(demopreviewdebug) conoutf("%s", text);
                        break;
                    }

                    default:
                        return;
                }
            }
        }

        void demoinfo::assignteams()
        {
            loopv(clients)
            {
                extplayer *d = getclient(i);
                if(!d) continue;
                if(d->state == CS_SPECTATOR) spectator.add(d);
                else
                {
                    extteam *t = newteam(d->team, -1);
                    if(t)t->players.add(d);
                    else if(teams.length() && teams[0]) teams[0]->players.add(d);
                }
            }
            if(!m_check(mode, M_TEAM) && teams.length() > 1)
            {
                loopv(teams[1]->players) if(teams[1]->players[i]) teams[0]->players.add(teams[1]->players[i]); //merge to one team
                teams.shrink(1);
            }
            teams.sort(extteam::compare);
            loopv(teams) teams[i]->players.sort(extplayer::compare);
        }

        bool demoinfo::isclanwar()
        {
            if(!m_check(mode, M_TEAM) || teams.length() < 2 || mastermode < MM_LOCKED) return false;

            char *firstclan = teams[0]->isclan();
            char *secondclan = teams[1]->isclan();
            if((!firstclan && !secondclan) || (teams[0]->players.length() != teams[1]->players.length())) return false;
            formatstring(infoline) ("\f6%s\f4 (%d)\f7 vs \f6%s\f4 (%d)", firstclan ? firstclan : "mix", teams[0]->score, secondclan ? secondclan : "mix" , teams[1]->score);
            copystring(gametype, "\f3clanwar");
            return true;
        }

        bool demoinfo::isduel()
        {
            if(m_check(mode, M_TEAM) || mastermode < MM_LOCKED) return false;
            if(!teams.inrange(0) || teams[0]->players.length() != 2) return false;
            formatstring(infoline) ("\f6%s\f4 (%d)\f7 vs \f6%s\f4 (%d)", teams[0]->players[0]->name, teams[0]->players[0]->frags, teams[0]->players[1]->name, teams[0]->players[1]->frags);
            copystring(gametype, "\f0duel");
            return true;
        }

        void demoinfo::analyze()
        {
            if(isduel() || isclanwar()) return;
            extplayer *mfrags = 0;
            loopvk(teams) loopv(teams[k]->players) if(!mfrags || teams[k]->players[i]->frags > mfrags->frags) mfrags = teams[k]->players[i];
            if(!mfrags) formatstring(infoline) ("\f4no info");
            else formatstring(infoline) ("most frags \f6%s\f4 (%d)", mfrags->name, mfrags->frags);
            copystring(gametype, "\f2mixed");
        }

        void demoinfo::endreading()
        {
            if(!file) return;
            DELETEP(file);
        }

        bool demoinfo::setupreading(char *filename)
        {
            if(file) endreading();
            demoheader hdr;
            string msg;

            msg[0] = '\0';
            defformatstring(fn)("%s", game::demospath(filename));
            file = opengzfile(fn, "rb");
            if(!file) formatstring(msg)("could not cache demo \"%s\"", fn);
            else if(file->read(&hdr, sizeof(demoheader))!=sizeof(demoheader) || (memcmp(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic)) && memcmp(hdr.magic, QUALITY_DEMO_MAGIC, sizeof(hdr.magic))))
                formatstring(msg)("\"%s\" is not a demo file (magic: %s)", fn, hdr.magic);
            else
            {
                lilswap(&hdr.version, 2);
                if(hdr.version==QUALITY_DEMO_VERSION && hdr.protocol==QUALITY_PROTOCOL_VERSION) goto qmsgout;
                if(hdr.version!=DEMO_VERSION) formatstring(msg)("demo \"%s\" requires an %s version of Cube 2: Sauerbraten", fn, hdr.version<DEMO_VERSION ? "older" : "newer");
                else if(hdr.protocol!=PROTOCOL_VERSION) formatstring(msg)("demo \"%s\" requires an %s version of Cube 2: Sauerbraten", fn, hdr.protocol<PROTOCOL_VERSION ? "older" : "newer");
            }

            goto msgout;
            qmsgout:
            if(debugquality && debugdemo)
                conoutf("demo: quality-demo: %s, MAGIC: %s, VERSION: %d, PROTOCOL: %d", fn, hdr.magic, hdr.version, hdr.protocol);

            msgout:
            if(msg[0])
            {
                DELETEP(file)
                conoutf(msg);
                return false;
            }

            if(file->read(&this->nextreading, sizeof(this->nextreading))!=sizeof(this->nextreading))
            {
                endreading();
                return false;
            }
            lilswap(&this->nextreading, 1);
            return true;
        }

        void demoinfo::load(char *demoname)
        {
            if(!setupreading(demoname)) return;
            while(file)
            {
                int chan, len;
                if(file->read(&chan, sizeof(chan))!=sizeof(chan) ||
                   file->read(&len, sizeof(len))!=sizeof(len))
                {
                    endreading();
                    break;
                }
                lilswap(&chan, 1);
                lilswap(&len, 1);

                ENetPacket *packet = enet_packet_create(NULL, (size_t)len+1, 0);
                if(!packet || file->read(packet->data+1, len)!=(size_t)len)
                {
                    if(packet) enet_packet_destroy(packet);
                    endreading();
                    break;
                }
                packet->data[0] = N_DEMOPACKET;
                if(chan == 1)
                {
                    packetbuf p(packet);
                    parsemsgs(-1, NULL, p);
                }
                if(!packet->referenceCount) enet_packet_destroy(packet);
                if(!file) break;
                if(file->read(&this->nextreading, sizeof(this->nextreading))!=sizeof(this->nextreading))
                {
                    endreading();
                    break;
                }
                lilswap(&this->nextreading, 1);
            }
            endreading();
            assignteams();
            analyze();
        }

        demoinfo *curdemoinfo = NULL;
        SDL_Thread *prevthread = NULL;
        SDL_mutex *prevmutex = NULL;
        SDL_cond *prevcond = NULL;

        struct previewdata
        {
            char *filename;
            demoinfo *dfile;
        };

        int previewthread(void *data)
        {
            SDL_LockMutex(prevmutex);
            if(!prevthread || SDL_GetThreadID(prevthread) != SDL_ThreadID())
            {
                SDL_UnlockMutex(prevmutex);
                return 0;
            }
            previewdata pd = *(previewdata *)data;
            SDL_UnlockMutex(prevmutex);

            demoinfo *d = new demoinfo();
            d->load(pd.filename);
            copystring(d->pathname, pd.filename);

            SDL_LockMutex(prevmutex);
            if(!prevthread || SDL_GetThreadID(prevthread) != SDL_ThreadID())
            {
                SDL_UnlockMutex(prevmutex);
                return 0;
            }
            ((previewdata *)data)->dfile = d;
            SDL_CondSignal(prevcond);
            SDL_UnlockMutex(prevmutex);

            return 0;
        }

        #define DEMOTIMEOUT 7000

        void demopreview(char *filename)
        {
            loopv(demoinfos) if(!strcmp(filename, demoinfos[i]->pathname))
            {
                curdemoinfo = demoinfos[i];
                return;
            }
            if(!prevmutex) prevmutex = SDL_CreateMutex();
            if(!prevcond) prevcond = SDL_CreateCond();
            SDL_LockMutex(prevmutex);
            previewdata pd = { newstring(filename), NULL};
            prevthread = SDL_CreateThread(previewthread, &pd);

            renderprogress(0, "Parsing demofiles");

            int starttime = SDL_GetTicks(), timeout = 0;
            for(;;)
            {
                if(!SDL_CondWaitTimeout(prevcond, prevmutex, 250))
                {
                    if(pd.dfile) demoinfos.add(pd.dfile);
                    break;
                }
                timeout = SDL_GetTicks() - starttime;
                if(timeout > DEMOTIMEOUT) break;

                renderprogress(min(float(timeout)/DEMOTIMEOUT, 1.0f), "Parsing demofiles");
            }
            prevthread = NULL;
            SDL_UnlockMutex(prevmutex);
        }

        ICOMMAND(demopreview, "s", (char *name), demopreview(name));

        VAR(demoeventscore, 0, 1, 1);
        VAR(demoeventtext, 0, 1, 1);
        VAR(demoeventfrags, 0, 1, 1);
        VAR(demoeventtime, 0, 1, 1);

        void getcurdemoinfopath()
        {
            if(!curdemoinfo) return;
            char fn[200];
            strcpy(fn, tempformatstring("%s", game::demospath(curdemoinfo->pathname)));
            cutextension(fn, (char *)".dmo");
            result(fn);
        }
        COMMAND(getcurdemoinfopath, "");

        int minz, secz;

        char *showdemohappenings(g3d_gui *cgui, uint *bottom)
        {
            if(!curdemoinfo) return NULL;
            demoinfo *d = curdemoinfo;

            for(int start = 0; start < d->events.length();)
            {
                if(start > 0) cgui->tab();
                int end = d->events.length();
                int numlines = 0;
                for(int j = start; j < end; j++)
                {
                    if(numlines >= 20) { end = j; break; }

                    demoinfo::happening *e = d->events[j];
                    if(!e || !e->text) continue;
                    int remainsec = ((m_check(d->mode, M_OVERTIME) ? 15 : 10) *60000 - e->time) / 1000;
                    int remainmin = remainsec / 60; remainsec = max(0, remainsec - remainmin * 60);
                    defformatstring(timestamp)("\f4%s%d:%s%d  ", remainmin<10? "0":"", remainmin, remainsec<10? "0":"", remainsec);
                    if(!demoeventtime) formatstring(timestamp)('\0');
                    extplayer *p = d->getclient(e->cn);
                    if(!p) continue;
                    switch(e->type)
                    {
                        case N_SCOREFLAG:
                        {
                            if(!demoeventscore ) break;
                            if(cgui->buttonf("%s%s%s \f2scored for team %s%s", 0xFFFF80, NULL, NULL, timestamp, !strcmp(p->team,"good")? "\f1":"\f3", p->name, !strcmp(e->text, "good") ? "\f1" : "\f3", e->text)&G3D_UP) { hastoupdate = true; minz = remainmin; secz = remainsec; demostartmin = max(0.0f, float(e->time/6000)*0.1f-0.1f); }
                            numlines++;
                            break;
                        }
                        case N_TEXT:
                        {
                            if(!demoeventtext) break;
                            if(cgui->buttonf("%s\f0%s:  \f7%s",0xFFFF80, NULL, NULL, timestamp, p->name, e->text)&G3D_UP) { hastoupdate = true; minz = remainmin; secz = remainsec; demostartmin = max(0.0f, float(e->time/6000)*0.1f-0.1f); }
                            numlines++;
                            break;
                        }
                        case N_DIED:
                        {
                            if(!demoeventfrags) break;
                            if((!strcmp(p->name, e->text) ? cgui->buttonf("%s%s%s \f2suicided",     0xFFFF80, NULL, NULL, timestamp, !strcmp(p->team, "good") ? "\f1" : "\f3", p->name) :
                                                            cgui->buttonf("%s%s%s \f7fragged \f2%s",0xFFFF80, NULL, NULL, timestamp, !strcmp(p->team, "good") ? "\f1" : "\f3", p->name, e->text ))&G3D_UP) { hastoupdate = true; minz = remainmin; secz = remainsec; demostartmin = max(0.0f, float(e->time/6000)*0.1f-0.1f); }
                            numlines++;
                            break;
                        }
                    }
                }
                if(bottom) execute(bottom);
                start = end;
            }

            return NULL;
        }

        char *showdemoinfo(g3d_gui *cgui)
        {
            if(!curdemoinfo) return NULL;
            demoinfo *d = curdemoinfo;

            cgui->pushlist();
            cgui->spring();
            cgui->text(server::prettymodename(d->mode), 0xFFFF80);
            cgui->separator();
            cgui->text(d->map[0] ? d->map : "[new map]", 0xFFFF80);
            cgui->spring();
            cgui->poplist();
            cgui->separator();

            listteams(cgui, d->teams, d->mode, true, true, true, true, false, false, true, true);

            if(d->spectator.length()) listspectators(cgui, d->spectator);
            return NULL;
        }

        int hitcount = 0;
        bool demobrowserstartcolumn(g3d_gui *g, int i)
        {
            static const char *names[] = { "file", "type ", "info", "mode ", "map ", "client ", "date " };
            static const int struts[] =  { 40,     10,      24, 	13,      14,     10,        10 };

            if(size_t(i) >= sizeof(names)/sizeof(names[0])) return false;
            g->pushlist();
            if(g->button(names[i], 0xFF00D0)&G3D_UP)
            {
                if(sortterm == i+1)
                {
                    if(hitcount %2)
                        upsidedown = false;
                    else
                        upsidedown = true;
                    hitcount++;
                }
                else
                {
                    upsidedown = false;
                    hitcount = 0;
                }
                sortterm = i+1;
            }
            g->strut(struts[i]);
            g->mergehits(true);
            return true;
        }

        bool demobrowserentry(g3d_gui *g, int i, char *type, char *info, char *filen, int mode, char *map, string _client, string date)
        {
            char fn[40];
            strncpy(fn, (const char *)filen, 39);
            switch(i)
            {
                case 0:
                    if(g->button(fn, 0xffffdd)&G3D_UP) return true;
                    break;
                case 1:
                    if(g->button(type, 0xffffdd)&G3D_UP) return true;
                    break;
                case 2:
                    if(g->button(info, 0xffffdd)&G3D_UP) return true;
                    break;
                case 3:
                    if(g->buttonf("%s", 0xffffdd, NULL, NULL, server::prettymodename(mode))&G3D_UP) return true;
                    break;
                case 4:
                    if(g->button(map, 0xffffdd)&G3D_UP) return true;
                    break;
                case 5:
                    if(g->button(_client, 0xffffdd)&G3D_UP) return true;
                    break;
                case 6:
                    if(g->button(date, 0xffffdd)&G3D_UP) return true;
                    break;
                // TODO: an easy way to delete the demos!

                default: break;
            }
            return false;
        }

        void demobrowserendcolumn(g3d_gui *g, int i)
        {
            g->mergehits(false);
            g->column(i);
            g->poplist();
        }

        SVAR(demofiltername, "");
        VAR(demofilterfrags, 0, 0, 500);

        bool demofilter(demoinfo *d)
        {
            if(!d) return false;
            if(demofiltername[0])
            {
                bool matchpl = false;
                char *pfil = strlwr(demofiltername);
                loopv(d->clients)
                {
                    if(!d->clients[i] || !d->clients[i]->name || !d->clients[i]->name[0]) continue;
                    char *cname = strlwr(d->clients[i]->name);
                    if(strstr(cname, pfil)) { matchpl = true; break; }
                }
                if(!matchpl)
                {
                    if(strstr(strlwr(d->map), pfil)) matchpl = true;
                    if(strstr(server::prettymodename(d->mode), pfil)) matchpl = true;
                    if(!matchpl) return false;
                }
            }
            if(demofilterfrags)
            {
                bool haspl = false;
                loopv(d->clients) if(d->clients[i] && d->clients[i]->frags > demofilterfrags) { haspl = true; break; }
                if(!haspl) return false;
            }
            return true;
        }

        char *demobrowser(g3d_gui *g)
        {
            bool selectedit = false;

            demoinfos.sort(demoinfo::compare);
            for(int start = 0; start < demoinfos.length();)
            {
                if(start > 0) g->tab();
                int end = demoinfos.length();
                g->pushlist();
                loopi(10)
                {
                    if(!demobrowserstartcolumn(g, i)) break;
                    for(int j = start; j < end; j++)
                    {
                        if(!i && g->shouldtab()) { end = j; break; }
                        demoinfo &d = *demoinfos[j];
                        if(demofilter(&d) && demobrowserentry(g, i, d.gametype, d.infoline, d.pathname, d.mode, d.map, d.client, d.date)) { curdemoinfo = &d; selectedit = true; }
                    }
                    demobrowserendcolumn(g, i);
                }
                g->poplist();
                start = end;
            }
            if(selectedit) return newstring("showgui demoext");
            return NULL;
        }

        const char *listteams(g3d_gui *cgui, vector<extteam *> &teams, int mode, bool icons, bool forcealive, bool frags, bool deaths, bool tks, bool acc, bool flags, bool cn, bool ping)
        {
            loopvk(teams)
            {
                if((k%2)==0) cgui->pushlist(); // horizontal

                extteam *tm = teams[k];
                int bgcolor = 0x3030C0, fgcolor = 0xFFFF80;
                int mybgcolor = 0xC03030;

                #define loopscoregroup(o, b) \
                    loopv(tm->players) \
                    { \
                        extplayer * o = tm->players[i]; \
                        b; \
                    }

                if(tm->name[0] && m_check(mode, M_TEAM))
                {
                    cgui->pushlist(); // vertical

                    cgui->pushlist();
                    cgui->background( (!k && teams.length()>1) ? bgcolor: mybgcolor, 1, 0);
                    if(tm->score>=10000) cgui->textf("   %s: WIN", fgcolor, NULL,NULL, tm->name);
                    else cgui->textf("   %s: %d", fgcolor, NULL, NULL, tm->name, tm->score);
                    cgui->poplist();

                    cgui->pushlist(); // horizontal
                }

                if(frags)
                {
                    cgui->pushlist();
                    cgui->strut(7);
                    cgui->text("frags", fgcolor);
                    loopscoregroup(o, cgui->textf("%d", 0xFFFFDD, NULL, NULL, o->frags));
                    cgui->poplist();
                }

                if(deaths)
                {
                    cgui->pushlist();
                    cgui->strut(7);
                    cgui->text("deaths", fgcolor);
                    loopscoregroup(o, cgui->textf("%d", 0xFFFFDD, NULL, NULL, o->deaths));
                    cgui->poplist();
                }

                if(tks)
                {
                    cgui->pushlist();
                    cgui->strut(7);
                    cgui->text("tks", fgcolor);
                    loopscoregroup(o, cgui->textf("%d", o->teamkills >= 5 ? 0xFF0000 : 0xFFFFDD, NULL, NULL, o->teamkills));
                    cgui->poplist();
                }

                if(acc)
                {
                    cgui->pushlist();
                    cgui->strut(7);
                    cgui->text("acc", fgcolor);
                    loopscoregroup(o, cgui->textf("%d%%", 0xFFFFDD, NULL, NULL, o->accuracy));
                    cgui->poplist();
                }

                if(flags && (m_check(mode, M_CTF) || m_check(mode, M_HOLD) || m_check(mode, M_PROTECT)))
                {
                    cgui->pushlist();
                    cgui->strut(7);
                    cgui->text("flags", fgcolor);
                    loopscoregroup(o, cgui->textf("%d", 0xFFFFDD, NULL, NULL, o->flags));
                    cgui->poplist();
                }

                if(ping)
                {
                    cgui->pushlist();
                    cgui->text("ping", fgcolor);
                    cgui->strut(6);
                    loopscoregroup(o,
                    {
                        if(o->state==CS_LAGGED) cgui->text("LAG", 0xFFFFDD);
                        else cgui->textf("%d", 0xFFFFDD, NULL, NULL, o->ping);
                    });
                    cgui->poplist();
                }

                cgui->pushlist();
                cgui->text("name", fgcolor);
                cgui->strut(10);
                loopscoregroup(o,
                {
                    int status = o->state!=CS_DEAD || forcealive ? 0xFFFFDD : 0x606060;
                    if(o->privilege)
                    {
                        status = o->privilege>=3 ? 0xFF8000 : (o->privilege==2 ? 0x40FFA0 : 0x40FF80 );
                        if(o->state==CS_DEAD && !forcealive) status = (status>>1)&0x7F7F7F;
                    }
                    if(o->cn < MAXCLIENTS) cgui->textf("%s", status, NULL, NULL, o->name);
                    else cgui->textf("%s \f5[%i]", status, NULL, NULL, o->name, o->cn);
                });
                cgui->poplist();

                if(cn)
                {
                    cgui->space(1);
                    cgui->pushlist();
                    cgui->text("cn", fgcolor);
                    loopscoregroup(o, cgui->textf("%d", 0xFFFFDD, NULL, NULL, o->cn));
                    cgui->poplist();
                }

                if(tm->name[0] && m_check(mode, M_TEAM))
                {
                    cgui->poplist(); // horizontal
                    cgui->poplist(); // vertical
                }

                if(k+1<teams.length() && (k+1)%2) cgui->space(2);
                else cgui->poplist(); // horizontal
            }
            return NULL;
        }

        const char *listspectators(g3d_gui *cgui, vector<extplayer *> &spectators, bool cn, bool ping)
        {
            cgui->pushlist();
            cgui->pushlist();
            cgui->text("spectator", 0xFFFF80);
            loopv(spectators)
            {
                extplayer *pl = spectators[i];
                int status = 0xFFFFDD;
                if(pl->privilege) status = pl->privilege>=2 ? 0xFF8000 : 0x40FF80;
                cgui->text(pl->name, status, "spectator");
            }
            cgui->poplist();

            if(cn)
            {
                cgui->space(1);
                cgui->pushlist();
                cgui->text("cn", 0xFFFF80);
                loopv(spectators) cgui->textf("%d", 0xFFFFDD, NULL, NULL,spectators[i]->cn);
                cgui->poplist();
            }
            if(ping)
            {
                cgui->space(1);
                cgui->pushlist();
                cgui->text("ping", 0xFFFF80);
                loopv(spectators) cgui->textf("%d", 0xFFFFDD, NULL,NULL, spectators[i]->ping);
                cgui->poplist();
            }
            cgui->poplist();
            return NULL;
        }
    }
}

