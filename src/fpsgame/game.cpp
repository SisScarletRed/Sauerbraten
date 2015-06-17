#include "game.h"
#include "engine.h"
#include "mod.h"
#include "extinfo.h"
#include "event.h"
#include "extendedscripts.h"

extern bool guiisshowing(),
            drawcradar;
extern int guifadein,
           getpacketloss(),
           fullconsole;

extern void screenshot(char *filename, bool justfilename, int dir),
            drawfragmsg(fpsent *d, int w, int h),
            drawcc(),
            resetphysics(),
            drawacoloredquad(float x, float y, float w, float h,
                             uchar r, uchar g, uchar b, uchar a);
VAR(deathcamerastate, 0, 0, 1);
VARP(deathcamera, 0, 0, 1);
VARP(newhud, 0, 1, 1);

int lasttimeupdate = 0;
float staticscale = 0.33f;
FVARFP(hudscale, 50, 100, 300, { staticscale = 0.33f*(hudscale/100.0f); });

namespace game
{
    bool clientoption(const char *arg) { return false; }
    bool intermission = false;
    int maptime = 0, maprealtime = 0, maplimit = -1;
    int respawnent = -1;
    int lasthit = 0, lastspawnattempt = 0;
    int following = -1, followdir = 0;
    int savedammo[NUMGUNS];

    fpsent *player1 = NULL;         // our client
    vector<fpsent *> players;       // other clients


    void taunt()
    {
        if(player1->state!=CS_ALIVE || player1->physstate<PHYS_SLOPE) return;
        if(lastmillis-player1->lasttaunt<1000) return;
        player1->lasttaunt = lastmillis;
        addmsg(N_TAUNT, "rc", player1);
    }
    COMMAND(taunt, "");

    ICOMMAND(getfollow, "", (),
    {
        fpsent *f = followingplayer();
        intret(f ? f->clientnum : -1);
    });

    int getstalked(int *cn)
    {
        fpsent *d = getclient(*cn);
        if(!d || d->state!=CS_SPECTATOR) return -1;

        loopv(players)
        {
            if(players[i]->state!=CS_SPECTATOR && players[i]->o==d->o)
                return players[i]->clientnum;
        }
        return -1;
    }
    ICOMMAND(getstalkedperson, "i", (int *cn), intret(getstalked(cn)));

	void follow(char *arg)
    {
        if(arg[0] ? player1->state==CS_SPECTATOR : following>=0)
        {
            following = arg[0] ? parseplayer(arg) : -1;
            if(following==player1->clientnum) following = -1;
            followdir = 0;
            conoutf("follow %s", following>=0 ? "on" : "off");
        }
	}
    COMMAND(follow, "s");

    void nextfollow(int dir)
    {
        if(player1->state!=CS_SPECTATOR || clients.empty())
        {
            stopfollowing();
            return;
        }
        int cur = following >= 0 ? following : (dir < 0 ? clients.length() - 1 : 0);
        loopv(clients)
        {
            cur = (cur + dir + clients.length()) % clients.length();
            if(clients[cur] && clients[cur]->state!=CS_SPECTATOR)
            {
                if(following<0) conoutf("follow on");
                following = cur;
                followdir = dir;
                return;
            }
        }
        stopfollowing();
    }
    ICOMMAND(nextfollow, "i", (int *dir), nextfollow(*dir < 0 ? -1 : 1));


    const char *getclientmap() { return clientmap; }

    void resetgamestate()
    {
        if(m_classicsp)
        {
            clearmovables();
            clearmonsters();                 // all monsters back at their spawns for editing
            entities::resettriggers();
        }
        clearprojectiles();
        clearbouncers();
    }

    fpsent *spawnstate(fpsent *d)              // reset player state not persistent accross spawns
    {
        d->respawn();
        d->spawnstate(gamemode);
        return d;
    }

    bool autorespawn = false;
    void respawnself()
    {
        if(ispaused()) return;
        if(m_mp(gamemode))
        {
            int seq = (player1->lifesequence<<16)|((lastmillis/1000)&0xFFFF);
            if(player1->respawned!=seq) { addmsg(N_TRYSPAWN, "rc", player1); player1->respawned = seq; }
        }
        else
        {
            spawnplayer(player1);
            showscores(false);
            lasthit = 0;
            if(cmode) cmode->respawned(player1);
        }
    }

    fpsent *pointatplayer()
    {
        loopv(players) if(players[i] != player1 && intersect(players[i], player1->o, worldpos)) return players[i];
        return NULL;
    }

    void stopfollowing()
    {
        if(following<0) return;
        following = -1;
        followdir = 0;
        conoutf("follow off");
    }

    fpsent *followingplayer()
    {
        if(player1->state!=CS_SPECTATOR || following<0) return NULL;
        fpsent *target = getclient(following);
        if(target && target->state!=CS_SPECTATOR) return target;
        return NULL;
    }

    fpsent *hudplayer()
    {
        if(thirdperson) return player1;
        fpsent *target = followingplayer();
        return target ? target : player1;
    }

    void setupcamera()
    {
        fpsent *target = followingplayer();
        if(target)
        {
            player1->yaw = target->yaw;
            player1->pitch = target->state==CS_DEAD ? 0 : target->pitch;
            player1->o = target->o;
            player1->resetinterp();
        }
    }

    bool detachcamera()
    {
        fpsent *d = hudplayer();
        return d->state==CS_DEAD;
    }

    bool collidecamera()
    {
        switch(player1->state)
        {
            case CS_EDITING: return false;
            case CS_SPECTATOR: return followingplayer()!=NULL;
        }
        return true;
    }

    VARP(smoothmove, 0, 75, 100);
    VARP(smoothdist, 0, 32, 64);

    void predictplayer(fpsent *d, bool move)
    {
        d->o = d->newpos;
        d->yaw = d->newyaw;
        d->pitch = d->newpitch;
        d->roll = d->newroll;
        if(move)
        {
            moveplayer(d, 1, false);
            d->newpos = d->o;
        }
        float k = 1.0f - float(lastmillis - d->smoothmillis)/smoothmove;
        if(k>0)
        {
            d->o.add(vec(d->deltapos).mul(k));
            d->yaw += d->deltayaw*k;
            if(d->yaw<0) d->yaw += 360;
            else if(d->yaw>=360) d->yaw -= 360;
            d->pitch += d->deltapitch*k;
            d->roll += d->deltaroll*k;
        }
    }

    VAR(_autoswitchclient, -1, -1, 127);
    VAR(_autoswitchmode, 0, 0, 2);

    ICOMMAND(autoswitchclientteam, "i", (int *cn), _autoswitchclient = *cn; _autoswitchmode = 1;);
    ICOMMAND(autounswitchclientteam, "i", (int *cn), _autoswitchclient = *cn; _autoswitchmode = 2;);

    VARP(autocheckwhois, 0, 0, 1);

    void otherplayers(int curtime)
    {
        loopv(players)
        {
            fpsent *d = players[i];
            if(totalmillis-d->connectedmillis > 4000 && !d->whoischecked && autocheckwhois)
            {
                whois::whois(d->clientnum);
                d->whoischecked = true;
            }
            d->speeddata.addspeed(d->state==CS_DEAD || intermission ? 0 : d->vel.magnitude2());

            if(d==hudplayer())
            {
                if(d->health<=25 && d->state==CS_ALIVE && !m_insta)
                {
                    d->hurtchan = playsound(S_HEARTBEAT, NULL, NULL, 0, -1, 1000, d->hurtchan);
                    damageblend(1, true);
                }
                else
                {
                    d->stopheartbeat();
                    removedamagescreen();
                }
            }

            if(players[i]->clientnum==_autoswitchclient)
            {
                if(_autoswitchmode==1 && strcmp(players[i]->team, player1->team))
                    switchteam(players[i]->team);
            }
            if(_autoswitchmode==2 && !strcmp(players[i]->team, player1->team))
                !strcmp(player1->team, "good") ? switchteam("evil") : switchteam("good");

            if(d == player1 || d->ai) continue;

            if(d->state==CS_DEAD && d->ragdoll) moveragdoll(d);
            else if(!intermission || (intermission && player1->state == CS_SPECTATOR))
            {
                if(lastmillis - d->lastaction >= d->gunwait) d->gunwait = 0;
                if(d->quadmillis) entities::checkquad(curtime, d);
            }

            const int lagtime = totalmillis-d->lastupdate;
            if(!lagtime || intermission) continue;
            else if(lagtime>1000 && d->state==CS_ALIVE)
            {
                d->state = CS_LAGGED;
                continue;
            }
            if(d->state==CS_ALIVE || d->state==CS_EDITING)
            {
                if(smoothmove && d->smoothmillis>0) predictplayer(d, true);
                else moveplayer(d, 1, false);
            }
            else if(d->state==CS_DEAD && !d->ragdoll && lastmillis-d->lastpain<2000) moveplayer(d, 1, true);
        }
    }

    VARFP(slowmosp, 0, 0, 1, { if(m_sp && !slowmosp) server::forcegamespeed(100); });

    void checkslowmo()
    {
        static int lastslowmohealth = 0;
        server::forcegamespeed(intermission ? 100 : clamp(player1->health, 25, 200));
        if(player1->health<player1->maxhealth && lastmillis-max(maptime, lastslowmohealth)>player1->health*player1->health/2)
        {
            lastslowmohealth = lastmillis;
            player1->health++;
        }
    }

    VARP(spawnwait, 0, 0, 1000);

    void respawn()
    {
        if(player1->state==CS_DEAD)
        {
            player1->attacking = false;
            int wait = cmode ? cmode->respawnwait(player1) : 0;
            if(wait>0)
            {
                lastspawnattempt = lastmillis;
                return;
            }
            if(lastmillis < player1->lastpain + spawnwait) return;
            if(m_dmsp) { changemap(clientmap, gamemode); return; }    // if we die in SP we try the same map again
            respawnself();
            autorespawn = false;
            if(m_classicsp)
            {
                conoutf(CON_GAMEINFO, "%sYou wasted another life! The monsters stole your armour and some ammo...", getmsgcolorstring());
                loopi(NUMGUNS) if(i!=GUN_PISTOL && (player1->ammo[i] = savedammo[i]) > 5) player1->ammo[i] = max(player1->ammo[i]/3, 5);
            }
        }
    }

    VARP(jump_autorespawn_set, 0, 1, 1);

    void setautorespawn()
    {
        if(!jump_autorespawn_set || player1->state != CS_DEAD) return;

        autorespawn = !autorespawn;
    }

    vector<namestruct *> friends;
    vector<namestruct *> clantags;

    void addclan(const char *clantag)
    {
        loopv(clantags) if(strstr(clantags[i]->name, clantag)) return;
        namestruct *c = new namestruct;
        strncpy(c->name, clantag, 16);
        c->name[15] = '\0';
        loopi(15) if(c->name[i]=='\'' || c->name[i]=='"' || c->name[i]=='[' || c->name[i]==']') c->name[i] = ' ';
        filtertext(c->name, c->name, true, false, 16);
        clantags.add(c);
    }

    void addfriend(const char *name)
    {
        loopv(friends) if(strstr(friends[i]->name, name)) return;
        namestruct *n = new namestruct;
        strncpy(n->name, name, 16);
        n->name[15] = '\0';
        friends.add(n);
    }

    ICOMMAND(addclan, "s", (const char *tag), addclan(tag));
    ICOMMAND(addfriend, "s", (const char *name), addfriend(name));

    VARP(cutclantags, 0, 1, 1);
    VARP(autoscreenshot_cw, 0, 1, 1);
    VARP(autoscreenshot_duel, 0, 1, 1);
    VARP(autoscreenshot_separatedirs, 0, 1, 1);
    VARP(intermissiontext, 0, 1, 1);

    string battleheadline;
    char *makefilename(const char *input)
    {
        string output;
        int len = min(259, (int)strlen(input));
        loopi(len)
        {
            if((int)input[i] == 34 ||
               (int)input[i] == 42 ||
               (int)input[i] == 47 ||
               (int)input[i] == 58 ||
               (int)input[i] == 60 ||
               (int)input[i] == 62 ||
               (int)input[i] == 63 ||
               (int)input[i] == 92 ||
               (int)input[i] == 124)
            {
                output[i] = ' ';
            }
            else output[i] = input[i];
        }
        output[len] = '\0';
        filtertext(output, output, false);
        return newstring(output);
    }

    const char *cutclantag(const char *name)
    {
        char curname[20];
        strcpy(curname, name);
        loopv(clantags) if(strstr(curname, clantags[i]->name))
        {
            char *pch;
            pch = strstr(curname, clantags[i]->name);
            strncpy(pch, "                    ", strlen(clantags[i]->name));
            puts(curname);
            filtertext(curname, curname, false, false, 19);
            return newstring(curname);
        }
        return name;
    }
    ICOMMAND(cutclantag, "s", (const char *name), result(cutclantag(name)));

    vector<fpsent *> duelplayers;
    bool isduel(bool allowspec = false, int colors = 0)
    {
        extern int mastermode;
        if((!allowspec && player1->state==CS_SPECTATOR) || mastermode < MM_LOCKED || m_teammode) return false;

        int playingguys = 0;
        duelplayers.setsize(0);
        fpsent *p1 = NULL, *p2 = NULL;
        loopv(players) if(players[i]->state != CS_SPECTATOR)
        {
            playingguys++;
            if(p1) p2 = players[i];
            else if(playingguys > 2) break;
            else p1 = players[i];
        }
        if(playingguys != 2) return false;

        duelplayers.add(p1);
        duelplayers.add(p2);

        string output;
        const char *p1name = cutclantags ? cutclantag(p1->name) : p1->name;
        const char *p2name = cutclantags ? cutclantag(p2->name) : p2->name;
        fpsent *f = followingplayer();

        if(!f && player1->state != CS_SPECTATOR) f = player1;
        if(!colors) formatstring(output)("%s(%d) vs %s(%d)", p1name, p1->frags, p2name, p2->frags);
        else if(colors==1 || !f)
            formatstring(output)("\f2%s\f7(%d) \f4vs \f1%s\f7(%d)", p1name, p1->frags, p2name, p2->frags);
        else
        {
            bool winning = (f==p1 && p1->frags > p2->frags) || (f==p2 && p1->frags < p2->frags);
            formatstring(output)("%s%s (\fs%s%d\fr) vs %s (\fs%s%d\fr)", winning ? "\f0" : "\f3", p1name, winning ? "\f0" : "\f3", p1->frags, p2name, winning ? "\f0" : "\f3", p2->frags);
        }
        strcpy(battleheadline, output);

        return true;
    }

    struct autoevent
    {
        int start,
            time,
            type;
        string say;

        autoevent()
        {
            start = time = type = 0;
            copystring(say, "unkown");
        }
    };
    vector<autoevent> autoevents;

    enum
    {
        A_TEAMCHAT,
        A_CHAT,
        A_SCREENSHOT
    };
    enum
    {
        DIR_NORMAL,
        DIR_DUEL,
        DIR_CLANWAR
    };

    extern void sayteam(char *text);

    void checkautoevents()
    {
        loopv(autoevents)
        {
            if(totalmillis-autoevents[i].start > autoevents[i].time)
            {
                switch(autoevents[i].type)
                {
                    case A_TEAMCHAT:
                        sayteam(autoevents[i].say);
                        break;
                    case A_CHAT:
                        toserver(autoevents[i].say);
                        break;
                    case A_SCREENSHOT:
                    {
                        if(!autoscreenshot_duel && !autoscreenshot_cw && !intermissiontext) break;
                        bool iscw = autoscreenshot_cw ? isclanwar() : false;
                        bool isdl = autoscreenshot_duel ? isduel() : false;

                        if(intermissiontext) conoutf(battleheadline);
                        if(!iscw && !isdl) break;
                        else if(iscw && !autoscreenshot_cw) break;
                        else if(isdl && !autoscreenshot_duel) break;

                        int prev = guifadein;
                        guifadein = 0;
                        showscores(true);
                        screenshot(makefilename(battleheadline), false, autoscreenshot_separatedirs ? (iscw ? DIR_CLANWAR : DIR_DUEL) : DIR_NORMAL);
                        guifadein = prev;
                        break;
                    }
                    default:
                        conoutf("There's a problem with autoevents ^^");
                }
                autoevents.remove(i);
            }
        }
    }

    VARP(autofollowactioncn, 0, 0, 1);
    VARP(actioncnfollowtime, 500, 5000, 60000);
    extern int getactioncn(bool cubescript = false);
    void followactioncn()
    {
        static int last = 0;

        if(!autofollowactioncn || player1->state != CS_SPECTATOR)
            return;

        fpsent *d;

        if(totalmillis-last < actioncnfollowtime &&
           (d = getclient(following)) &&
           d->state == CS_ALIVE)
            return;

        int cn = getactioncn();

        if(cn < 0)
            return;

        last = totalmillis;

        following = cn;
    }

    QVARP(radarhidewithgui, "hide the cmode-radar when a menu is showing", 0, 1, 1);
    QVARP(radarhidewithscoreboard, "hide the cmode-radar when sb is showing", 0, 1, 1);
    QVARP(radarhidewithintermission, "hide the cmode-radar on intermission", 0, 0, 1);

    extern void autodemocheck();
    void updateworld()        // main game update loop
    {
        if(!maptime) { maptime = lastmillis; maprealtime = totalmillis; return; }
        if(!curtime) { gets2c(); if(player1->clientnum>=0) c2sinfo(); return; }

        checkautoevents();
        if(autorespawn) respawn();
        physicsframe();
        ai::navigate();
        if(player1->state != CS_DEAD && !intermission)
        {
            if(player1->quadmillis) entities::checkquad(curtime, player1);
        }
        updateweapons(curtime);
        otherplayers(curtime);
        ai::update();
        moveragdolls();
        gets2c();
        updatemovables(curtime);
        updatemonsters(curtime);
        followactioncn();
        if(connected)
        {
            drawcradar = !((radarhidewithgui && guiisshowing()) || (radarhidewithscoreboard && getvar("scoreboard")) || (radarhidewithintermission && intermission));
            if(player1->state == CS_DEAD)
            {
                if(player1->ragdoll) moveragdoll(player1);
                else if(lastmillis-player1->lastpain < 2000)
                {
                    player1->move = player1->strafe = 0;
                    moveplayer(player1, 10, true);
                }
            }
            else if(!intermission)
            {
                if(player1->ragdoll) cleanragdoll(player1);
                moveplayer(player1, 10, true);
                swayhudgun(curtime);
                entities::checkitems(player1);
                if(m_sp)
                {
                    if(slowmosp) checkslowmo();
                    if(m_classicsp) entities::checktriggers();
                }
                else if(cmode) cmode->checkitems(player1);
            }
        }
        autodemocheck();
        if(player1->clientnum>=0) c2sinfo();   // do this last, to reduce the effective frame lag
    }

    void spawnplayer(fpsent *d)   // place at random spawn
    {
        if(cmode) cmode->pickspawn(d);
        else findplayerspawn(d, d==player1 && respawnent>=0 ? respawnent : -1);
        spawnstate(d);
        if(d==player1)
        {
            if(editmode) d->state = CS_EDITING;
            else if(d->state != CS_SPECTATOR) d->state = CS_ALIVE;
        }
        else d->state = CS_ALIVE;
    }

    // inputs

    void doattack(bool on)
    {
        if(intermission) return;
        if((player1->attacking = on)) respawn();
        if(deathcamerastate)
        {
            deathcamerastate = 0;
            following = -1;
            followdir = 0;
            player1->state=CS_DEAD;
            respawn();
        }
    }

    bool canjump()
    {
        if(!intermission) respawn();
        if(deathcamerastate)
        {
            deathcamerastate = 0;
            following = -1;
            followdir = 0;
            player1->state = CS_DEAD;
            respawn();
        }
        return player1->state!=CS_DEAD && !intermission;
    }

    bool allowmove(physent *d)
    {
        if(d->type!=ENT_PLAYER) return true;
        return !((fpsent *)d)->lasttaunt || lastmillis-((fpsent *)d)->lasttaunt>=1000;
    }

    VARP(hitsound, 0, 0, 1);
    VARP(damagemotion, 0, 1, 1);
    VAR(inmotion, 0, 0, 1);

    void dodamagemotion()
    {
        inmotion = 1;

        player1->roll = player1->roll+(rand()%2?+360:-360);

        execute("sleep 666 [motionblur $_motionblur]");
        execute("sleep 666 [motionblurmillis $_motionblurmillis]");
        execute("sleep 666 [motionblurscale $_motionblurscale]");
        execute("sleep 666 [inmotion 0]");
    }

    void damaged(int damage, fpsent *d, fpsent *actor, bool local)
    {
        if(d == player1 && damagemotion && !inmotion && (actor->gunselect == GUN_RL || actor->gunselect == GUN_GL))
        {
            execute("_motionblur = $motionblur");
            execute("_motionblurmillis = $motionblurmillis");
            execute("_motionblurscale = $motionblurscale");

            execute("motionblur 1");
            execute("motionblurmillis 100");
            execute("motionblurscale 0.5");

            dodamagemotion();
        }
        if((d->state!=CS_ALIVE && d->state != CS_LAGGED && d->state != CS_SPAWNING) || intermission) return;

        if(local) damage = d->dodamage(damage);
        else if(actor==player1) return;

        fpsent *h = hudplayer();
        if(h!=player1 && actor==h && d!=actor)
        {
            if(hitsound && lasthit != lastmillis) playsound(S_HIT);
            lasthit = lastmillis;
        }
        if(d==h)
        {
            damageblend(damage);
            damagecompass(damage, actor->o);
        }
        damageeffect(damage, d, d!=h);

		ai::damaged(d, actor);

        if(m_sp && slowmosp && d==player1 && d->health < 1) d->health = 1;

        if(d->health<=0) { if(local) killed(d, actor); }
        else if(d==h)
            playsound(lookupmaterial(camera1->o)!=MAT_WATER ? S_PAIN1+rnd(5) : S_UWPN4+rnd(3));
        else
        {
            if(lookupmaterial(camera1->o)!=MAT_WATER)
                playsound(lookupmaterial(d->o)!=MAT_WATER ? S_PAIN1+rnd(5) : S_UWPN4+rnd(3), &d->o);
            else
                playsound(S_UWPN4+rnd(3), &d->o);
        }
    }

    VARP(deathscore, 0, 1, 1);

    void deathstate(fpsent *d, bool restore)
    {
        d->state = CS_DEAD;
        d->lastpain = lastmillis;
        if(!restore) gibeffect(max(-d->health, 0), d->vel, d);
        if(d==player1)
        {
            if(deathscore) showscores(true);
            disablezoom();
            if(!restore) loopi(NUMGUNS) savedammo[i] = player1->ammo[i];
            d->attacking = false;
            if(!restore) d->deaths++;
            //d->pitch = 0;
            d->roll = 0;
            if(lookupmaterial(camera1->o)!=MAT_WATER) playsound(S_DIE1+rnd(2));
        }
        else
        {
            d->move = d->strafe = 0;
            d->resetinterp();
            d->smoothmillis = 0;
            if(lookupmaterial(camera1->o)!=MAT_WATER && lookupmaterial(d->o)!=MAT_WATER) playsound(S_DIE1+rnd(2), &d->o);
        }
    }

    VARP(teamcolorfrags, 0, 1, 1);
    VARP(guncolorfrags, 0, 1, 1);
    VARP(indentfrags, 0, 1, 1);

    SVARP(autosorrymsg, "sorry");
    SVARP(autonpmsg, "np");
    SVARP(autoggmsg, "gg");

    VARP(autosorry, 0, 1, 1);
    VARP(autonp, 0, 1, 1);
    VARP(autogg, 0, 1, 1);

    VARP(autosorrydelay, 0, 3, 10);
    VARP(autonpdelay, 0, 3, 10);
    VARP(autoggdelay, 0, 3, 10);

    VARP(sendname, 0, 0, 1);

    void killed(fpsent *d, fpsent *actor)
    {
        const char *you = "";
        if(m_teammode && teamcolorfrags) you = "\fs\f1You\fr";
        else you = "\fs\f0You\fr";

        const char *indent = indentfrags ? "\t" : "";

        const char *fragged = "";
        if(!guncolorfrags || m_insta) fragged = "\fs\f2fragged\fr";
        if(guncolorfrags && !m_insta && actor->gunselect==GUN_FIST)   fragged = "\fs\f2dismembered\fr";
        if(guncolorfrags && !m_insta && actor->gunselect==GUN_SG)     fragged = "\fs\f3peppered\fr";
        if(guncolorfrags && !m_insta && actor->gunselect==GUN_CG)     fragged = "\fs\f0shredded\fr";
        if(guncolorfrags && !m_insta && actor->gunselect==GUN_RIFLE)  fragged = "\fs\f1punctured\fr";
        if(guncolorfrags && !m_insta && actor->gunselect==GUN_RL)     fragged = "\fs\f6nuked\fr";
        if(guncolorfrags && !m_insta && actor->gunselect==GUN_GL)     fragged = "\fs\ftbusted\fr";
        if(guncolorfrags && !m_insta && actor->gunselect==GUN_PISTOL) fragged = "\fs\f4shot\fr";

        if(d->state==CS_EDITING)
        {
            d->editstate = CS_DEAD;
            if(d==player1) d->deaths++;
            else d->resetinterp();
            return;
        }
        else if((d->state!=CS_ALIVE && d->state != CS_LAGGED && d->state != CS_SPAWNING) || intermission) return;

        event::run(event::PLAYER_FRAG, tempformatstring("%d", actor->clientnum));

        fpsent *h = followingplayer();
        if(!h) h = player1;
        int contype = d==h || actor==h ? CON_FRAG_SELF : CON_FRAG_OTHER;
        const char *dname = "", *aname = "";
        if(m_teammode && teamcolorfrags)
        {
            dname = teamcolorname(d, "You");
            aname = teamcolorname(actor, "You");
        }
        else
        {
            dname = colorname(d, NULL, "", "", "You");
            aname = colorname(actor, NULL, "", "", "You");
        }
        if(actor->type==ENT_AI)
            conoutf(contype, "\f2%s%s got %s by %s!", indent, you, fragged, aname);
        else if(d==actor || actor->type==ENT_INANIMATE)
        {
            if(d==player1) statslog[STATS_SUIS]++;
            conoutf(contype, "\f2%s%s suicided%s", indent, dname, d==player1 ? "!" : "");
        }
        else if(isteam(d->team, actor->team))
        {
            event::run(event::PLAYER_TEAM_KILL, tempformatstring("%d", actor->clientnum));
            contype |= CON_TEAMKILL;
            if(actor==player1)
            {
                conoutf(contype, "\f6%s%s %s your teammate (%s)", indent, you, fragged, dname);
                statslog[STATS_TKS]++;
                if(autosorry && d->type==ENT_PLAYER)
                {
                    defformatstring(sry)("%s %s", autosorrymsg, sendname ? cutclantags ? cutclantag(dname) : dname : "");
                    autoevent &a = autoevents.add();
                    a.start = totalmillis;
                    a.time = autosorrydelay*1000;
                    copystring(a.say, newstring(sry));
                    a.type = A_TEAMCHAT;
                }
                playsound(S_TEAMKILL);
            }
            else if(d==player1)
            {
                conoutf(contype, "\f6%s%s got %s by your teammate (%s)", indent, you, fragged, aname);
                statslog[STATS_GOT_TK]++;
                if(autonp && actor->type==ENT_PLAYER)
                {
                    defformatstring(np)("%s %s", autonpmsg, sendname ? cutclantags ? cutclantag(aname) : aname : "");
                    autoevent &a = autoevents.add();
                    a.start = totalmillis;
                    a.time = autonpdelay*1000;
                    copystring(a.say, newstring(np));
                    a.type = A_TEAMCHAT;
                }
            }
            else conoutf(contype, "\f2%s%s %s a teammate (%s)", indent, aname, fragged, dname);
        }
        else
        {
            if(d==player1)
            {
                statslog[STATS_DEATHS]++;
                conoutf(contype, "\f2%s%s got %s by %s", indent, you, fragged, aname);
            }
            else
            {
                conoutf(contype, "\f2%s%s %s %s", indent, aname, fragged, dname);
                if(actor==player1)
                {
                    if(d->vel.magnitude() >= 190) playsound(S_GREATSHOT);
                    else playsound(S_KILL);
                    crosshairbump();
                    statslog[STATS_FRAGS]++;
                    if(player1->gunselect==0) statslog[STATS_CHAINFRAGS]++;
                }
            }
        }
        deathstate(d);
		ai::killed(d, actor);

        if(d==player1 && actor!=player1 && deathcamera)
        {
            deathcamerastate = 1;
            showscores(false);
            player1->state = CS_SPECTATOR;
            following = actor->clientnum;
            followdir = 0;
        }
    }

    void timeupdate(int secs)
    {
        if(secs > 0) maplimit = lastmillis + secs*1000;
        else
        {
            intermission = true;
            player1->attacking = false;
            if(cmode) cmode->gameover();
            conoutf(CON_GAMEINFO, "%sintermission:", getmsgcolorstring());
            conoutf(CON_GAMEINFO, "%sgame has ended!", getmsgcolorstring());
            if(m_ctf) conoutf(CON_GAMEINFO, "%splayer frags: %d, flags: %d, deaths: %d", getmsgcolorstring(), player1->frags, player1->flags, player1->deaths);
            else if(m_collect) conoutf(CON_GAMEINFO, "%splayer frags: %d, skulls: %d, deaths: %d", getmsgcolorstring(), player1->frags, player1->flags, player1->deaths);
            else conoutf(CON_GAMEINFO, "%splayer frags: %d, deaths: %d", getmsgcolorstring(), player1->frags, player1->deaths);
            int accuracy = (player1->totaldamage*100)/max(player1->totalshots, 1);
            conoutf(CON_GAMEINFO, "%splayer total damage dealt: %d, damage wasted: %d, accuracy(%%): %d", getmsgcolorstring(), player1->totaldamage, player1->totalshots-player1->totaldamage, accuracy);
            if(m_sp) spsummary(accuracy);

            showscores(true);
            disablezoom();

            if(autoscreenshot_duel || autoscreenshot_cw)
            {
                string versus = "buf";
                autoevent &a = autoevents.add();
                a.start = totalmillis;
                a.time = 1000;
                copystring(a.say, newstring(versus));
                a.type = A_SCREENSHOT;
            }
            if(autogg)
            {
                autoevent &a = autoevents.add();
                a.start = totalmillis;
                a.time = autoggdelay*1000;
                copystring(a.say, newstring(autoggmsg));
                a.type = A_CHAT;
            }
            if(isclanwar()) statslog[STATS_CLANWARS]++;
            else if(isduel()) statslog[STATS_DUELS]++;
            if(identexists("intermission")) execute("intermission");
            event::run(event::INTERMISSION);
        }
    }

    ICOMMAND(statslog, "i", (int *i), intret(statslog[*i]));

    const char *statsfile() { return "saved/stats.ini"; }
    void loadstats()
    {
        stream *f = openutf8file(path(statsfile(), true), "r");
		if(!f)
        {
			loopi(STATS_NUM) statslog[i] = 0;
			return;
		}
		char buf[255] = "";
		loopi(STATS_NUM)
        {
			f->getline(buf, sizeof(buf));
			statslog[i] = atoi(buf);
		}
		DELETEP(f);
    }
	COMMAND(writestats, "");

	void writestats()
	{
		stream *f = openutf8file(path(statsfile(), true), "w");
		if(!f) { conoutf("not able to save stats"); return; }
		string s;
		loopi(STATS_NUM)
		{
			formatstring(s)("%d",statslog[i]);
			f->putline(s);
		}
		DELETEP(f)
	}
	COMMAND(loadstats, "");

	void clearstats()
	{
		loopi(STATS_NUM) statslog[i] = 0;
	}
	COMMAND(clearstats, "");

	void dotime()
	{
		if(totalmillis < lasttimeupdate+1000) return;
		statslog[STATS_SECONDS]++;

		if(statslog[STATS_SECONDS] >= 60)
		{
			statslog[STATS_SECONDS] = 0;
			statslog[STATS_MINUTES]++;
		}

		if(statslog[STATS_MINUTES] >= 60)
		{
			statslog[STATS_MINUTES] = 0;
			statslog[STATS_HOURS]++;
		}

		if(statslog[STATS_HOURS] >= 24)
		{
			statslog[STATS_HOURS] = 0;
			statslog[STATS_DAYS]++;
		}

		if(statslog[STATS_DAYS] >= 7)
		{
			statslog[STATS_DAYS] = 0;
			statslog[STATS_WEEKS]++;
		}
		lasttimeupdate = totalmillis;
	}

    ICOMMAND(testintermission, "", (), timeupdate(0));

    ICOMMAND(getaccuracy, "", (), intret((player1->totaldamage*100)/max(player1->totalshots, 1)));
    ICOMMAND(getarmour, "", (), intret(player1->armour));
    ICOMMAND(getcamposx, "", (), intret(player1->o.x));
    ICOMMAND(getcamposy, "", (), intret(player1->o.y));
    ICOMMAND(getcamposz, "", (), intret(player1->o.z));
    ICOMMAND(getdeaths, "", (), intret(player1->deaths));
    ICOMMAND(getflags, "", (), intret(player1->flags));
    ICOMMAND(getfrags, "", (), intret(player1->frags));
    ICOMMAND(getgunselect, "", (), intret(player1->gunselect));
    ICOMMAND(gethealth, "", (), intret(player1->health));
    ICOMMAND(getphysstate, "", (), intret(player1->physstate));
    ICOMMAND(getping, "", (), intret(player1->ping));
    ICOMMAND(getpitch, "", (), intret(player1->pitch));
    ICOMMAND(getspeed, "", (), intret(player1->vel.magnitude()));
    ICOMMAND(getstate, "", (), intret(player1->state));
    ICOMMAND(getsuicides, "", (), intret(player1->suicides));
    //ICOMMAND(getteamkills, "", (), intret(player1->teamkills));
    ICOMMAND(gettotaldamage, "", (), intret(player1->totaldamage));
    ICOMMAND(gettotalshots, "", (), intret(player1->totalshots));
    ICOMMAND(getvelx, "", (), intret(player1->vel.x));
    ICOMMAND(getvely, "", (), intret(player1->vel.y));
    ICOMMAND(getvelz, "", (), intret(player1->vel.z));
    ICOMMAND(getyaw, "", (), intret(player1->yaw));

    int getteamscore(const char *team)
    {
        vector<teamscore> teamscores;
        if(cmode) cmode->getteamscores(teamscores);
        else loopv(players) if(players[i]->team[0])
        {
            fpsent *player = players[i];
            teamscore *ts = NULL;
            loopvj(teamscores) if(!strcmp(teamscores[j].team, player->team)) { ts = &teamscores[j]; break; }
            if(!ts) teamscores.add(teamscore(player->team, player->frags));
            else ts->score += player->frags;
        }
        loopv(teamscores)
        {
            if(!strcmp(teamscores[i].team, team))
            {
                return teamscores[i].score;
            }
        }
        return 0;
    }
    ICOMMAND(getteamscore, "s", (const char *team), intret(getteamscore(team)));

    vector<fpsent *> clients;

    fpsent *newclient(int cn)   // ensure valid entity
    {
        if(cn < 0 || cn > max(0xFF, MAXCLIENTS + MAXBOTS))
        {
            neterr("clientnum", false);
            return NULL;
        }

        if(cn == player1->clientnum) return player1;

        while(cn >= clients.length()) clients.add(NULL);
        if(!clients[cn])
        {
            fpsent *d = new fpsent;
            d->clientnum = cn;
            clients[cn] = d;
            players.add(d);
        }
        return clients[cn];
    }

    fpsent *getclient(int cn)   // ensure valid entity
    {
        if(cn == player1->clientnum) return player1;
        return clients.inrange(cn) ? clients[cn] : NULL;
    }

    void clientdisconnected(int cn, bool notify)
    {
        if(!clients.inrange(cn)) return;
        if(following==cn)
        {
            if(followdir) nextfollow(followdir);
            else stopfollowing();
        }
        unignore(cn);
        fpsent *d = clients[cn];
        if(!d) return;
        if(notify && d->name[0])
        {
            conoutf("\f4disconnect:\f7 %s", colorname(d));
            event::run(event::PLAYER_DISCONNECT, tempformatstring("%d", d->clientnum));
        }
        removeweapons(d);
        removetrackedparticles(d);
        removetrackeddynlights(d);
        if(cmode) cmode->removeplayer(d);
        players.removeobj(d);
        DELETEP(clients[cn]);
        cleardynentcache();
    }

    void clearclients(bool notify)
    {
        loopv(clients) if(clients[i]) clientdisconnected(i, notify);
    }

    void initclient()
    {
        player1 = spawnstate(new fpsent);
        filtertext(player1->name, "unnamed", false, false, MAXNAMELEN);
        players.add(player1);
    }

    VARP(showmodeinfo, 0, 1, 1);

    void startgame()
    {
        clearmovables();
        clearmonsters();

        clearprojectiles();
        clearbouncers();
        clearragdolls();

        clearteaminfo();

        // reset perma-state
        loopv(players)
        {
            fpsent *d = players[i];
            d->frags = d->flags = 0;
            d->deaths = d->suicides = d->teamkills = 0;
            d->totaldamage = 0;
            d->totalshots = 0;
            d->maxhealth = 100;
            d->lifesequence = -1;
            d->respawned = d->suicided = -2;
            d->lastvictim = d->lastkiller = d->lastdamagecauser = NULL;
            d->lastfragtime = d->lastdeathtime = 0;
            d->lastfragweapon = d->lastdeathweapon = d->lastddweapon = d->lastdrweapon = -1;
        }

        setclientmode();

        intermission = false;
        maptime = maprealtime = 0;
        maplimit = -1;

        if(cmode)
        {
            cmode->preload();
            cmode->setup();
        }

        conoutf(CON_GAMEINFO, "%sgame mode is %s", getmsgcolorstring(), server::prettymodename(gamemode));

        if(m_sp)
        {
            defformatstring(scorename)("bestscore_%s", getclientmap());
            const char *best = getalias(scorename);
            if(*best) conoutf(CON_GAMEINFO, "%stry to beat your best score so far: %s", getmsgcolorstring(), best);
        }
        else
        {
            const char *info = m_valid(gamemode) ? gamemodes[gamemode - STARTGAMEMODE].info : NULL;
            if(showmodeinfo && info) conoutf(CON_GAMEINFO, "%s%s", getmsgcolorstring(), info);
        }

        if(player1->playermodel != playermodel) switchplayermodel(playermodel);

        showscores(false);
        disablezoom();
        lasthit = 0;

        if(identexists("mapstart")) execute("mapstart");
        event::run(event::MAPSTART, game::getclientmap());
    }

    void startmap(const char *name)   // called just after a map load
    {
        deathcamerastate = 0;
        if(!m_edit) resetphysics();

        ai::savewaypoints();
        ai::clearwaypoints(true);

        respawnent = -1; // so we don't respawn at an old spot
        if(!m_mp(gamemode)) spawnplayer(player1);
        else findplayerspawn(player1, -1);
        entities::resetspawns();
        copystring(clientmap, name ? name : "");

        sendmapinfo();
    }

    const char *getmapinfo()
    {
        return showmodeinfo && m_valid(gamemode) ? gamemodes[gamemode - STARTGAMEMODE].info : NULL;
    }

    void physicstrigger(physent *d, bool local, int floorlevel, int waterlevel, int material)
    {
        if(d->type==ENT_INANIMATE) return;
        if     (waterlevel>0) { if(material!=MAT_LAVA) playsound(S_SPLASH1, d==player1 ? NULL : &d->o); }
        else if(waterlevel<0) playsound(material==MAT_LAVA ? S_BURN : S_SPLASH2, d==player1 ? NULL : &d->o);
        if     (floorlevel>0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai) { msgsound(S_JUMP, d); if(d==player1) statslog[STATS_JUMPS]++; } }
        else if(floorlevel<0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai) msgsound(S_LAND, d); }
    }

    void dynentcollide(physent *d, physent *o, const vec &dir)
    {
        switch(d->type)
        {
            case ENT_AI: if(dir.z > 0) stackmonster((monster *)d, o); break;
            case ENT_INANIMATE: if(dir.z > 0) stackmovable((movable *)d, o); break;
        }
    }

    void msgsound(int n, physent *d)
    {
        if(!d || d==player1)
        {
            addmsg(N_SOUND, "ci", d, n);
            playsound(n);
        }
        else
        {
            if(d->type==ENT_PLAYER && ((fpsent *)d)->ai)
                addmsg(N_SOUND, "ci", d, n);
            playsound(n, &d->o);
        }
    }

    int numdynents() { return players.length()+monsters.length()+movables.length(); }

    dynent *iterdynents(int i)
    {
        if(i<players.length()) return players[i];
        i -= players.length();
        if(i<monsters.length()) return (dynent *)monsters[i];
        i -= monsters.length();
        if(i<movables.length()) return (dynent *)movables[i];
        return NULL;
    }

    bool duplicatename(fpsent *d, const char *name = NULL, const char *alt = NULL)
    {
        if(!name) name = d->name;
        if(alt && d != player1 && !strcmp(name, alt)) return true;
        loopv(players) if(d!=players[i] && !strcmp(name, players[i]->name)) return true;
        return false;
    }

    static string cname[3];
    static int cidx = 0;

    const char *colorname(fpsent *d, const char *name, const char *prefix, const char *suffix, const char *alt)
    {
        if(!name) name = alt && d == player1 ? alt : d->name;
        bool dup = !name[0] || duplicatename(d, name, alt) || d->aitype != AI_NONE;
        if(dup || prefix[0] || suffix[0])
        {
            cidx = (cidx+1)%3;
            if(dup) formatstring(cname[cidx])(d->aitype == AI_NONE ? "%s%s \fs\f5(%d)\fr%s" : "%s%s \fs\f5[%d]\fr%s", prefix, name, d->clientnum, suffix);
            else formatstring(cname[cidx])("%s%s%s", prefix, name, suffix);
            return cname[cidx];
        }
        return name;
    }

    VARP(teamcolortext, 0, 1, 1);

    VARP(playerteamcolor, 0, 1, 9);
    VARP(enemyteamcolor, 0, 3, 9);
    VARP(gamemessagescolor, 0, 2, 9);

    #define MAX_COLORS 10
    const char *colorstrings[MAX_COLORS] = { "\f0", "\f1", "\f2", "\f3", "\f4",
                                             "\f5", "\f6", "\f7", "\f8", "\f9"};
    const char *getcolorstring(uint n, bool cubescript)
    {
        if(cubescript)
        {
            if(n < MAX_COLORS)
                return escapestring(colorstrings[n]);
            return escapestring(colorstrings[7]);
        }
        if(n < MAX_COLORS)
            return colorstrings[n];
        return colorstrings[7];
    }
    ICOMMAND(getcolorstring, "i", (int *n), result(getcolorstring((uint)*n, true)));

    const char *getmsgcolorstring()
    {
        return colorstrings[gamemessagescolor];
    }

    static const char *getteamcolorstring(bool sameteam)
    {
        if(!sameteam)
            return colorstrings[enemyteamcolor];
        else
            return colorstrings[playerteamcolor];
    }

    const char *teamcolorname(fpsent *d, const char *alt)
    {
        static char b[10];
        if(!teamcolortext || !m_teammode) return colorname(d, NULL, "", "", alt);
        sprintf(b, "\fs%s", getteamcolorstring(isteam(d->team, player1->team)));
        return colorname(d, NULL, b, "\fr", alt);
    }

    const char *teamcolor(const char *name, bool sameteam, const char *alt)
    {
        if(!teamcolortext || !m_teammode) return sameteam || !alt ? name : alt;
        cidx = (cidx+1)%3;
        formatstring(cname[cidx])("\fs%s%s\fr",
                     getteamcolorstring(sameteam),
                     sameteam || !alt ? name : alt);
        return cname[cidx];
    }

    const char *teamcolor(const char *name, const char *team, const char *alt)
    {
        return teamcolor(name, team && isteam(team, player1->team), alt);
    }

    void suicide(physent *d)
    {
        if(d==player1 || (d->type==ENT_PLAYER && ((fpsent *)d)->ai))
        {
            if(d->state!=CS_ALIVE) return;
            fpsent *pl = (fpsent *)d;
            if(!m_mp(gamemode)) killed(pl, pl);
            else
            {
                int seq = (pl->lifesequence<<16)|((lastmillis/1000)&0xFFFF);
                if(pl->suicided!=seq) { addmsg(N_SUICIDE, "rc", pl); pl->suicided = seq; }
            }
        }
        else if(d->type==ENT_AI) suicidemonster((monster *)d);
        else if(d->type==ENT_INANIMATE) suicidemovable((movable *)d);
    }
    ICOMMAND(suicide, "", (), suicide(player1));

    bool needminimap() { return m_ctf || m_protect || m_hold || m_capture || m_collect; }

    int guiprivcolor(int priv)
    {
        switch(priv)
        {
            case PRIV_MASTER: return COLOR_MASTER;
            case PRIV_AUTH:   return COLOR_AUTH;
            case PRIV_ADMIN:  return COLOR_ADMIN;
        }
        return COLOR_NORMAL;
    }

    void drawicon(int icon, float x, float y, float sz)
    {
        settexture("packages/hud/items.png");
        glBegin(GL_TRIANGLE_STRIP);
        float tsz = 0.25f, tx = tsz*(icon%4), ty = tsz*(icon/4);
        glTexCoord2f(tx,     ty);     glVertex2f(x,    y);
        glTexCoord2f(tx+tsz, ty);     glVertex2f(x+sz, y);
        glTexCoord2f(tx,     ty+tsz); glVertex2f(x,    y+sz);
        glTexCoord2f(tx+tsz, ty+tsz); glVertex2f(x+sz, y+sz);
        glEnd();
    }

    void drawimage(const char *image, float xs, float ys, float xe, float ye)
    {
        if(!settexture(image)) return;
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(xs, ys);
        glTexCoord2f(1, 0); glVertex2f(xe, ys);
        glTexCoord2f(0, 1); glVertex2f(xs, ye);
        glTexCoord2f(1, 1); glVertex2f(xe, ye);
        glEnd();
    }

    VARP(newhud_abovehud, 800, 925, 1000);
    float abovegameplayhud(int w, int h)
    {
        switch(hudplayer()->state)
        {
            case CS_EDITING:
            case CS_SPECTATOR:
                return 1;
            default:
                if(newhud)
                    return newhud_abovehud/1000.f;
                return 1650.0f/1800.0f;
        }
    }

    vector <hudelement *> hudelements;

    void addhudelement(int *type, float *xpos, float *ypos, float *xscale, float *yscale, const char *script)
    {
        if(script[0]) hudelements.add(new hudelement(*type, *xpos, *ypos, *xscale, *yscale, script));
    }
    COMMAND(addhudelement, "iffffs");
    ICOMMAND(listhudelements, "", (), loopv(hudelements) conoutf("%d %f %f %f %f %s", hudelements[i]->type, hudelements[i]->xpos, hudelements[i]->ypos, hudelements[i]->xscale, hudelements[i]->yscale, hudelements[i]->script));

    int ammohudup[3] = { GUN_CG, GUN_RL, GUN_GL },
        ammohuddown[3] = { GUN_RIFLE, GUN_SG, GUN_PISTOL },
        ammohudcycle[7] = { -1, -1, -1, -1, -1, -1, -1 };

    ICOMMAND(ammohudup, "V", (tagval *args, int numargs),
    {
        loopi(3) ammohudup[i] = i < numargs ? getweapon(args[i].getstr()) : -1;
    });

    ICOMMAND(ammohuddown, "V", (tagval *args, int numargs),
    {
        loopi(3) ammohuddown[i] = i < numargs ? getweapon(args[i].getstr()) : -1;
    });

    ICOMMAND(ammohudcycle, "V", (tagval *args, int numargs),
    {
        loopi(7) ammohudcycle[i] = i < numargs ? getweapon(args[i].getstr()) : -1;
    });

    VARP(ammobar, 0, 0, 1);
    VARP(ammobardisablewithgui, 0, 0, 1);
    VARP(ammobardisablewithscoreboard, 0, 1, 1);
    VARP(ammobardisableininsta, 0, 0, 1);
    VARP(ammobarfilterempty, 0, 0, 1);
    VARP(ammobariconspos, -1, -1, 1);
    VARP(ammobarsize, 1, 5, 30);
    VARP(ammobaroffset_x, 0, 10, 1000);
    VARP(ammobaroffset_start_x, -1, -1, 1);
    VARP(ammobaroffset_y, 0, 500, 1000);
    VARP(ammobarhorizontal, 0, 0, 1);
    VARP(ammobarselectedcolor_r, 0, 100, 255);
    VARP(ammobarselectedcolor_g, 0, 200, 255);
    VARP(ammobarselectedcolor_b, 0, 255, 255);
    VARP(ammobarselectedcolor_a, 0, 150, 255);
    VARP(coloredammo, 0, 0, 1);

    VARP(ammohud, 0, 1, 1);

    VARP(coloredhealth, 0, 0, 1);

    VARP(newhud_hpdisableininsta, 0, 0, 1);
    VARP(newhud_hpdisablewithgui, 0, 0, 1);
    VARP(newhud_hpdisablewithscoreboard, 0, 1, 1);
    VARP(newhud_hpssize, 0, 30, 50);
    VARP(newhud_hpgap, 0, 100, 200);
    VARP(newhud_hpiconssize, 0, 60, 200);
    VARP(newhud_hppos_x, 0, 420, 1000);
    VARP(newhud_hppos_y, 0, 960, 1000);

    VARP(newhud_ammodisable, 0, 0, 1);
    VARP(newhud_ammodisableininsta, -1, 0, 1);
    VARP(newhud_ammodisablewithgui, 0, 0, 1);
    VARP(newhud_ammodisablewithscoreboard, 0, 1, 1);
    VARP(newhud_ammosize, 0, 30, 50);
    VARP(newhud_ammoiconssize, 0, 60, 200);
    VARP(newhud_ammogap, 0, 100, 200);
    VARP(newhud_ammopos_x, 0, 580, 1000);
    VARP(newhud_ammopos_y, 0, 960, 1000);

    VARP(gameclock, 0, 0, 1);
    VARP(gameclockdisablewithgui, 0, 0, 1);
    VARP(gameclockdisablewithscoreboard, 0, 1, 1);
    VARP(gameclocksize, 1, 5, 30);
    VARP(gameclockoffset_x, 0, 10, 1000);
    VARP(gameclockoffset_start_x, -1, 1, 1);
    VARP(gameclockoffset_y, 0, 300, 1000);
    VARP(gameclockcolor_r, 0, 255, 255);
    VARP(gameclockcolor_g, 0, 255, 255);
    VARP(gameclockcolor_b, 0, 255, 255);
    VARP(gameclockcolor_a, 0, 255, 255);
    VARP(gameclockcolorbg_r, 0, 100, 255);
    VARP(gameclockcolorbg_g, 0, 200, 255);
    VARP(gameclockcolorbg_b, 0, 255, 255);
    VARP(gameclockcolorbg_a, 0, 50, 255);

    VARP(hudscores, 0, 0, 1);
    VARP(hudscoresdisablewithgui, 0, 0, 1);
    VARP(hudscoresdisablewithscoreboard, 0, 1, 1);
    VARP(hudscoresvertical, 0, 0, 1);
    VARP(hudscoressize, 1, 5, 30);
    VARP(hudscoresoffset_x, 0, 10, 1000);
    VARP(hudscoresoffset_start_x, -1, 1, 1);
    VARP(hudscoresoffset_y, 0, 350, 1000);
    VARP(hudscoresplayercolor_r, 0, 0, 255);
    VARP(hudscoresplayercolor_g, 0, 255, 255);
    VARP(hudscoresplayercolor_b, 0, 255, 255);
    VARP(hudscoresplayercolor_a, 0, 255, 255);
    VARP(hudscoresplayercolorbg_r, 0, 0, 255);
    VARP(hudscoresplayercolorbg_g, 0, 255, 255);
    VARP(hudscoresplayercolorbg_b, 0, 255, 255);
    VARP(hudscoresplayercolorbg_a, 0, 50, 255);
    VARP(hudscoresenemycolor_r, 0, 255, 255);
    VARP(hudscoresenemycolor_g, 0, 0, 255);
    VARP(hudscoresenemycolor_b, 0, 0, 255);
    VARP(hudscoresenemycolor_a, 0, 255, 255);
    VARP(hudscoresenemycolorbg_r, 0, 255, 255);
    VARP(hudscoresenemycolorbg_g, 0, 85, 255);
    VARP(hudscoresenemycolorbg_b, 0, 85, 255);
    VARP(hudscoresenemycolorbg_a, 0, 50, 255);

    VARP(newhud_spectatorsdisablewithgui, 0, 1, 1);
    VARP(newhud_spectatorsdisablewithscoreboard, 0, 1, 1);
    VARP(newhud_spectatorsnocolor, 0, 1, 1);
    VARP(newhud_spectatorsize, 0, 5, 30);
    VARP(newhud_spectatorpos_x, 0, 500, 1000);
    VARP(newhud_spectatorpos_start_x, -1, 0, 1);
    VARP(newhud_spectatorpos_y, 0, 110, 1000);

    VARP(newhud_itemsdisablewithgui, 0, 0, 1);
    VARP(newhud_itemsdisablewithscoreboard, 0, 1, 1);
    VARP(newhud_itemssize, 0, 20, 50);
    VARP(newhud_itemspos_x, 0, 10, 1000);
    VARP(newhud_itemspos_reverse_x, 0, 1, 1);
    VARP(newhud_itemspos_centerfirst, 0, 0, 1);
    VARP(newhud_itemspos_y, 0, 920, 1000);

    VARP(lagometer, 0, 0, 1);
    VARP(lagometernobg, 0, 0, 1);
    VARP(lagometerdisablewithgui, 0, 1, 1);
    VARP(lagometerdisablewithscoreboard, 0, 1, 1);
    VARP(lagometerdisablelocal, 0, 1, 1);
    VARP(lagometershowping, 0, 1, 1);
    VARP(lagometeronlypingself, 0, 1, 1);
    VARP(lagometerpingsz, 100, 150, 200);
    VARP(lagometerpos_x, 0, 10, 1000);
    VARP(lagometerpos_start_x, -1, 1, 1);
    VARP(lagometerpos_y, 0, 500, 1000);
    VARP(lagometerlen, 100, 100, LAGMETERDATASIZE);
    VARP(lagometerheight, 50, 100, 300);
    VARP(lagometercolsz, 1, 1, 3);

    VARP(speedometer, 0, 0, 1);
    VARP(speedometernobg, 0, 0, 1);
    VARP(speedometerdisablewithgui, 0, 1, 1);
    VARP(speedometerdisablewithscoreboard, 0, 1, 1);
    VARP(speedometershowspeed, 0, 1, 1);
    VARP(speedometeronlyspeedself, 0, 1, 1);
    VARP(speedometerspeedsz, 100, 150, 200);
    VARP(speedometerpos_x, 0, 10, 1000);
    VARP(speedometerpos_start_x, -1, 1, 1);
    VARP(speedometerpos_y, 0, 500, 1000);
    VARP(speedometerlen, 100, 100, SPEEDMETERDATASIZE);
    VARP(speedometerheight, 50, 100, 300);
    VARP(speedometercolsz, 1, 1, 3);

    VARP(fragmsg, 0, 0, 1);
    VARP(fragmsgdisablewithgui, 0, 1, 1);
    VARP(fragmsgdisablewithscoreboard, 0, 1, 1);
    VARP(fragmsgfade, 0, 1200, 10000);

    VARP(sehud, 0, 1, 1);
    VARP(sehuddisablewithgui, 0, 1, 1);
    VARP(sehuddisablewithscoreboard, 0, 1, 1);
    VARP(sehuddisableininsta, 0, 0, 1);
    VARP(sehudshowhealth, 0, 1, 1);
    VARP(sehudshowarmour, 0, 1, 1);
    VARP(sehudshowammo, 0, 1, 1);
    VARP(sehudshowitems, 0, 1, 1);

    VARP(hudstats, 0, 1, 1);
    VARP(hudstatsalpha, 0, 200, 255);
    VARP(hudstatsoffx, -1000,  10, 1000);
    VARP(hudstatsoffy, -1000, 450, 1000);
    VARP(hudstatscolor, 0, 9, 9);
    VARP(hudstatsdisablewithgui, 0, 0, 1);
    VARP(hudstatsdisablewithscoreboard, 0, 1, 1);

    VARP(hudline, 0, 1, 1);
    VARP(hudlinecolor, 0, 2, 2);
    VARP(hudlinescale, 3, 8, 18);
    VARP(hudlineminiscoreboard, 0, 1, 1);
    VARP(hudlinedisablewithgui, 0, 0, 1);
    VARP(hudlinedisablewithscoreboard, 0, 1, 1);

    static const int stdshowspectators = 1;
    static const int stdfontscale = 25;
    static const int stdrightoffset = 0;
    static const int stdheightoffset = 250;
    static const int stdwidthoffset = 0;
    static const int stdlineoffset = 0;
    static const int stdplayerlimit = 32;
    static const int stdmaxteams = 6;
    static const int stdmaxnamelen = 13;
    static const int stdalpha = 150;

    static float fontscale = stdfontscale/100.0f;

    VARP(showplayerdisplay, 0, 0, 1);
    VARP(playerdisplayshowspectators, 0, stdshowspectators, 1);
    VARFP(playerdisplayfontscale, 25, stdfontscale, 30, fontscale = playerdisplayfontscale/100.0f);
    VARP(playerdisplayrightoffset, -100, stdrightoffset, 100);
    VARP(playerdisplayheightoffset, 0, stdheightoffset, 600);
    VARP(playerdisplaywidthoffset, -50, stdwidthoffset, 300);
    VARP(playerdisplaylineoffset, -30, stdlineoffset, 50);
    VARP(playerdisplayplayerlimit, 1, stdplayerlimit, 64);
    VARP(playerdisplaymaxteams, 1, stdmaxteams, 16);
    VARP(playerdisplaymaxnamelen, 4, stdmaxnamelen, MAXNAMELEN);
    VARP(playerdisplayalpha, 20, stdalpha, 0xFF);

    static void playerdisplayreset()
    {
        playerdisplayshowspectators = stdshowspectators;
        playerdisplayfontscale = stdfontscale;
        playerdisplayrightoffset = stdrightoffset;
        playerdisplayheightoffset = stdheightoffset;
        playerdisplaywidthoffset = stdwidthoffset;
        playerdisplaylineoffset = stdlineoffset;
        playerdisplayplayerlimit = stdplayerlimit;
        playerdisplaymaxteams = stdmaxteams;
        playerdisplaymaxnamelen = stdmaxnamelen;
        playerdisplayalpha = stdalpha;
    }
    COMMAND(playerdisplayreset, "");

    ICOMMAND(extendedsettings, "", (), executestr("showgui extended_settings"));

    void getammocolor(fpsent *d, int gun, int &r, int &g, int &b, int &a)
    {
        if(!d) return;
        if(gun == 0)
            r = 255, g = 255, b = 255, a = 255;
        else if(gun == 2 || gun == 6)
        {
            if(d->ammo[gun] > 10)
                r = 255, g = 255, b = 255, a = 255;
            else if(d->ammo[gun] > 5)
                r = 255, g = 127, b = 0, a = 255;
            else
                r = 255, g = 0, b = 0, a = 255;
        }
        else
        {
            if(d->ammo[gun] > 4)
                r = 255, g = 255, b = 255, a = 255;
            else if(d->ammo[gun] > 2)
                r = 255, g = 127, b = 0, a = 255;
            else
                r = 255, g = 0, b = 0, a = 255;
        }
    }

    static void drawselectedammobg(float x, float y, float w, float h)
    {
        drawacoloredquad(x, y, w, h,
                         (GLubyte)ammobarselectedcolor_r,
                         (GLubyte)ammobarselectedcolor_g,
                         (GLubyte)ammobarselectedcolor_b,
                         (GLubyte)ammobarselectedcolor_a);
    }

    static inline int limitammo(int s)
    {
        return clamp(s, 0, 999);
    }

    static inline int limitscore(int s)
    {
        return clamp(s, -999, 9999);
    }

    void drawammobar(fpsent *d, int w, int h)
    {
        if(!d) return;
        #define NWEAPONS 6
        float conw = w/staticscale, conh = h/staticscale;

        int icons[NWEAPONS] = { GUN_SG, GUN_CG, GUN_RL, GUN_RIFLE, GUN_GL, GUN_PISTOL };

        int r = 255, g = 255, b = 255, a = 255;
        char buf[10];
        float ammobarscale = (1+ammobarsize/10.0)*h/1080.0,
              xoff = 0.0,
              yoff = ammobaroffset_y*conh/1000,
             vsep = 15,
             hsep = 15,
             hgap = ammobariconspos == 0 ? 40 : 60,
             vgap = ammobariconspos == 0 ? 30 : 20,
             textsep = 10;
        int pw = 0, ph = 0, tw = 0, th = 0;

        glPushMatrix();
        glScalef(staticscale*ammobarscale, staticscale*ammobarscale, 1);
        draw_text("", 0, 0, 255, 255, 255, 255);

        int szx = 0, szy = 0, eszx = 0, eszy = 0;
        text_bounds("999", pw, ph);

        if(ammobariconspos == 0)
        {
            eszx = pw;
            eszy = 2*ph + textsep;
        }
        else
        {
            eszx = ph + textsep + pw;
            eszy = ph;
        }

        if(ammobarhorizontal)
        {
            szx = NWEAPONS*(eszx+hgap) - hgap + hsep;
            szy = eszy + vsep;
        }
        else
        {
            szx = eszx + hsep;
            szy = NWEAPONS*(eszy+vgap) - vgap + vsep;
        }

        if(ammobaroffset_start_x == 1)
            xoff = (1000-ammobaroffset_x)*conw/1000 - szx * ammobarscale;
        else if(ammobaroffset_start_x == 0)
            xoff = ammobaroffset_x*conw/1000 - szx/2.0 * ammobarscale;
        else
            xoff = ammobaroffset_x*conw/1000;
        yoff -= szy/2.0 * ammobarscale;

        for(int i = 0, xpos = 0, ypos = 0; i < NWEAPONS; i++)
        {
            snprintf(buf, 10, "%d", limitammo(d->ammo[i+1]));
            text_bounds(buf, tw, th);
            draw_text("", 0, 0, 255, 255, 255, 255);
            if(i+1 == d->gunselect)
                drawselectedammobg(xoff/ammobarscale + xpos,
                                   yoff/ammobarscale + ypos,
                                   eszx + hsep,
                                   eszy + vsep);
            if(ammobarfilterempty && d->ammo[i+1] == 0)
                draw_text("", 0, 0, 255, 255, 255, 85);
            if(ammobariconspos == -1)
                drawicon(HICON_FIST+icons[i], xoff/ammobarscale + xpos + hsep/2.0, yoff/ammobarscale + ypos + vsep/2.0, ph);
            else if(ammobariconspos == 1)
                drawicon(HICON_FIST+icons[i], xoff/ammobarscale + xpos + eszx - ph - hsep/2.0, yoff/ammobarscale + ypos + vsep/2.0, ph);
            else
                drawicon(HICON_FIST+icons[i], xoff/ammobarscale + xpos + (pw-th)/2.0 + hsep/2.0, yoff/ammobarscale + ypos + 0.75*vsep, ph);
            if(coloredammo) getammocolor(d, i+1, r, g, b, a);
            if(!(ammobarfilterempty && d->ammo[i+1] == 0) )
            {
                if(ammobariconspos == -1)
                    draw_text(buf, xoff/ammobarscale + xpos + hsep/2.0 + ph + textsep + (pw-tw)/2.0,
                              yoff/ammobarscale + ypos + vsep/2.0, r, g, b, a);
                else if(ammobariconspos == 1)
                    draw_text(buf, xoff/ammobarscale + xpos + hsep/2.0 + (pw-tw)/2.0,
                              yoff/ammobarscale + ypos + vsep/2.0, r, g, b, a);
                else
                    draw_text(buf, xoff/ammobarscale + xpos + hsep/2.0 + (pw-tw)/2.0,
                              yoff/ammobarscale + ypos + 0.75*vsep + textsep + ph, r, g, b, a);
            }
            if(ammobarhorizontal)
                xpos += eszx + hgap;
            else
                ypos += eszy + vgap;
        }
        draw_text("", 0, 0, 255, 255, 255, 255);
        glPopMatrix();
        #undef NWEAPONS
    }

    void drawammohud(fpsent *d)
    {
        float x = HICON_X + 2*HICON_STEP, y = HICON_Y, sz = HICON_SIZE;
        glPushMatrix();
        glScalef(1/3.2f, 1/3.2f, 1);
        float xup = (x+sz)*3.2f, yup = y*3.2f + 0.1f*sz;
        loopi(3)
        {
            int gun = ammohudup[i];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->ammo[gun]) continue;
            drawicon(HICON_FIST+gun, xup, yup, sz);
            yup += sz;
        }
        float xdown = x*3.2f - sz, ydown = (y+sz)*3.2f - 0.1f*sz;
        loopi(3)
        {
            int gun = ammohuddown[3-i-1];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->ammo[gun]) continue;
            ydown -= sz;
            drawicon(HICON_FIST+gun, xdown, ydown, sz);
        }
        int offset = 0, num = 0;
        loopi(7)
        {
            int gun = ammohudcycle[i];
            if(gun < GUN_FIST || gun > GUN_PISTOL) continue;
            if(gun == d->gunselect) offset = i + 1;
            else if(d->ammo[gun]) num++;
        }
        float xcycle = (x+sz/2)*3.2f + 0.5f*num*sz, ycycle = y*3.2f-sz;
        loopi(7)
        {
            int gun = ammohudcycle[(i + offset)%7];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->ammo[gun]) continue;
            xcycle -= sz;
            drawicon(HICON_FIST+gun, xcycle, ycycle, sz);
        }
        glPopMatrix();
    }

    int composedhealth(fpsent *d)
    {
        if(d->armour)
        {
            double absorbk = (d->armourtype+1)*0.25;
            int d1 = d->health/(1.0 - absorbk);
            int d2 = d->health + d->armour;
            if(d1 < d->armour/absorbk) return d1; // more armor than health
            else return d2; // more health than armor
        }
        else return d->health;
    }

    void getchpcolors(fpsent *d, int& r, int& g, int& b, int& a)
    {
        int chp = d->state==CS_DEAD ? 0 : composedhealth(d);
        if(chp > 250)
            r = 0, g = 127, b = 255, a = 255;
        else if(chp > 200)
            r = 0, g = 255, b = 255, a = 255;
        else if(chp > 150)
            r = 0, g = 255, b = 127, a = 255;
        else if(chp > 100)
            r = 127, g = 255, b = 0, a = 255;
        else if(chp > 50)
            r = 255, g = 127, b = 0, a = 255;
        else
            r = 255, g = 0, b = 0, a = 255;
    }

    void drawhudicons(fpsent *d, int w, int h)
    {
        glPushMatrix();
        glScalef(h/1800.0f, h/1800.0f, 1);

        drawicon(HICON_HEALTH, HICON_X, HICON_Y);

        glPushMatrix();
        glScalef(2, 2, 1);

        char buf[10];
        int r = 255, g = 255, b = 255, a = 255;
        if(coloredhealth && !m_insta) getchpcolors(d, r, g, b, a);
        snprintf(buf, 10, "%d", d->state==CS_DEAD ? 0 : d->health);
        draw_text(buf, (HICON_X + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, r, g, b, a);
        if(d->state!=CS_DEAD)
        {
            if(d->armour)
            {
                snprintf(buf, 10, "%d", d->armour);
                draw_text(buf, (HICON_X + HICON_STEP + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, r, g, b, a);
            }
            r = 255, g = 255, b = 255, a = 255;
            snprintf(buf, 10, "%d", d->ammo[d->gunselect]);
            if(coloredammo && !m_insta) getammocolor(d, d->gunselect, r, g, b, a);
            draw_text(buf, (HICON_X + 2*HICON_STEP + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, r, g, b, a);
        }
        draw_text("", 0, 0, 255, 255, 255, 255);
        glPopMatrix();

        if(d->state!=CS_DEAD)
        {
            if(d->armour) drawicon(HICON_BLUE_ARMOUR+d->armourtype, HICON_X + HICON_STEP, HICON_Y);
            drawicon(HICON_FIST+d->gunselect, HICON_X + 2*HICON_STEP, HICON_Y);
            if(d->quadmillis) drawicon(HICON_QUAD, HICON_X + 3*HICON_STEP, HICON_Y);
            if(ammohud) drawammohud(d);
        }
        glPopMatrix();
    }

    void drawnewhudhp(fpsent *d, int w, int h)
    {
        if((m_insta && newhud_hpdisableininsta) ||
           (newhud_hpdisablewithgui && guiisshowing()) ||
           (newhud_hpdisablewithgui && getvar("scoreboard")))
            return;

        int conw = int(w/staticscale), conh = int(h/staticscale);
        float hpscale = (1 + newhud_hpssize/10.0)*h/1080.0;
        float xoff = newhud_hppos_x*conw/1000;
        float yoff = newhud_hppos_y*conh/1000;

        glPushMatrix();
        glScalef(staticscale*hpscale, staticscale*hpscale, 1);
        draw_text("", 0, 0, 255, 255, 255, 255);

        char buf[10];
        int r = 255, g = 255, b = 255, a = 255, tw = 0, th = 0;
        float hsep = 20.0*((float)newhud_hpgap/100.0f);
        if(coloredhealth && !m_insta) getchpcolors(d, r, g, b, a);
        snprintf(buf, 10, "%d", d->state==CS_DEAD ? 0 : d->health);
        text_bounds(buf, tw, th);

        float iconsz = th*newhud_hpiconssize/100.0;
        xoff -= iconsz/2.0*hpscale;
        draw_text(buf, xoff/hpscale - tw - hsep, yoff/hpscale - th/2.0, r, g, b, a);
        if(d->state!=CS_DEAD && d->armour)
        {
            draw_text("", 0, 0, 255, 255, 255, 255);
            drawicon(HICON_BLUE_ARMOUR+d->armourtype, xoff/hpscale, yoff/hpscale - iconsz/2.0, iconsz);
            snprintf(buf, 10, "%d", d->armour);
            draw_text(buf, xoff/hpscale + iconsz + hsep, yoff/hpscale - th/2.0, r, g, b, a);
        }
        draw_text("", 0, 0, 255, 255, 255, 255);

        glPopMatrix();
    }

    void drawnewhudammo(fpsent *d, int w, int h)
    {
        if(d->state==CS_DEAD ||
           newhud_ammodisable ||
           (m_insta && newhud_ammodisableininsta == 1) ||
           (!m_insta && newhud_ammodisableininsta == -1) ||
           (newhud_ammodisablewithgui && guiisshowing()) ||
           (newhud_ammodisablewithscoreboard && getvar("scoreboard")))
            return;

        int conw = int(w/staticscale), conh = int(h/staticscale);
        float ammoscale = (1 + newhud_ammosize/10.0)*h/1080.0;
        float xoff = newhud_ammopos_x*conw/1000;
        float yoff = newhud_ammopos_y*conh/1000;
        float hsep = 20.0*((float)newhud_ammogap/100.0f);
        int r = 255, g = 255, b = 255, a = 255, tw = 0, th = 0;

        glPushMatrix();
        glScalef(staticscale*ammoscale, staticscale*ammoscale, 1);
        draw_text("", 0, 0, 255, 255, 255, 255);

        char buf[10];
        snprintf(buf, 10, "%d", d->ammo[d->gunselect]);
        text_bounds(buf, tw, th);
        float iconsz = th*newhud_ammoiconssize/100.0;
        xoff -= iconsz/2.0*ammoscale;

        drawicon(HICON_FIST+d->gunselect, xoff/ammoscale, yoff/ammoscale - iconsz/2.0, iconsz);

        if(coloredammo && !m_insta) getammocolor(d, d->gunselect, r, g, b, a);
        draw_text(buf, xoff/ammoscale + hsep + iconsz, yoff/ammoscale - th/2.0, r, g, b, a);
        draw_text("", 0, 0, 255, 255, 255, 255);

        glPopMatrix();
    }

    void drawclock(int w, int h)
    {
        int conw = int(w/staticscale), conh = int(h/staticscale);

        char buf[10];
        int millis = max(game::maplimit-lastmillis, 0);
        int secs = millis/1000;
        int mins = secs/60;
        secs %= 60;
        snprintf(buf, 10, "%d:%02d", mins, secs);

        int r = gameclockcolor_r,
            g = gameclockcolor_g,
            b = gameclockcolor_b,
            a = gameclockcolor_a;
        float gameclockscale = (1 + gameclocksize/10.0)*h/1080.0;

        glPushMatrix();
        glScalef(staticscale*gameclockscale, staticscale*gameclockscale, 1);
        draw_text("", 0, 0, 255, 255, 255, 255);

        int tw = 0, th = 0;
        float xoff = 0.0, xpos = 0.0, ypos = 0.0;
        float yoff = gameclockoffset_y*conh/1000;
        float borderx = 10.0;
        text_bounds(buf, tw, th);

        if(gameclockoffset_start_x == 1)
        {
            xoff = (1000 - gameclockoffset_x)*conw/1000;
            xpos = xoff/gameclockscale - tw - 2.0 * borderx;
        }
        else if(gameclockoffset_start_x == 0)
        {
            xoff = gameclockoffset_x*conw/1000;
            xpos = xoff/gameclockscale - tw/2.0;
        }
        else
        {
            xoff = gameclockoffset_x*conw/1000;
            xpos = xoff/gameclockscale;
        }
        ypos = yoff/gameclockscale - th/2.0;

        drawacoloredquad(xpos,
                         ypos,
                         tw + 2.0*borderx,
                         th,
                         (GLubyte)gameclockcolorbg_r,
                         (GLubyte)gameclockcolorbg_g,
                         (GLubyte)gameclockcolorbg_b,
                         (GLubyte)gameclockcolorbg_a);
        draw_text(buf, xpos + borderx, ypos, r, g, b, a);
        draw_text("", 0, 0, 255, 255, 255, 255);

        glPopMatrix();
    }

    void drawscores(int w, int h)
    {
        int conw = int(w/staticscale), conh = int(h/staticscale);

        vector<fpsent *> bestplayers;
        vector<scoregroup *> bestgroups;
        int grsz = 0;

        if(m_teammode) { grsz = groupplayers(); bestgroups = getscoregroups(); }
        else { getbestplayers(bestplayers, true); grsz = bestplayers.length(); }

        float scorescale = (1 + hudscoressize/10.0)*h/1080.0;
        float xoff = hudscoresoffset_start_x == 1 ? (1000 - hudscoresoffset_x)*conw/1000 : hudscoresoffset_x*conw/1000;
        float yoff = hudscoresoffset_y*conh/1000;
        float scoresep = hudscoresvertical ? 30 : 40;
        float borderx = scoresep/2.0;

        int r1, g1, b1, a1, r2, g2, b2, a2,
            bgr1, bgg1, bgb1, bga1, bgr2, bgg2, bgb2, bga2;
        int tw1=0, th1=0, tw2=0, th2=0;

        if(grsz)
        {
            char buf1[5], buf2[5];
            int isbest=1;
            fpsent* currentplayer = (player1->state==CS_SPECTATOR) ? followingplayer() : player1;
            if(!currentplayer) return;

            if(m_teammode) isbest = ! strcmp(currentplayer->team, bestgroups[0]->team);
            else isbest = currentplayer == bestplayers[0];

            glPushMatrix();
            glScalef(staticscale*scorescale, staticscale*scorescale, 1);
            draw_text("", 0, 0, 255, 255, 255, 255);

            if(isbest)
            {
                int frags=0, frags2=0;
                if(m_teammode) frags = bestgroups[0]->score;
                else frags = bestplayers[0]->frags;
                frags = limitscore(frags);

                if(frags >= 9999)
                    snprintf(buf1, 5, "WIN");
                else
                    snprintf(buf1, 5, "%d", frags);
                text_bounds(buf1, tw1, th1);

                if(grsz > 1)
                {
                    if(m_teammode) frags2 = bestgroups[1]->score;
                    else frags2 = bestplayers[1]->frags;
                    frags2 = limitscore(frags2);

                    if(frags2 >= 9999)
                        snprintf(buf2, 5, "WIN");
                    else
                        snprintf(buf2, 5, "%d", frags2);
                    text_bounds(buf2, tw2, th2);
                }
                else
                {
                    snprintf(buf2, 5, " ");
                    text_bounds(buf2, tw2, th2);
                }

                r1 = hudscoresplayercolor_r;
                g1 = hudscoresplayercolor_g;
                b1 = hudscoresplayercolor_b;
                a1 = hudscoresplayercolor_a;

                r2 = hudscoresenemycolor_r;
                g2 = hudscoresenemycolor_g;
                b2 = hudscoresenemycolor_b;
                a2 = hudscoresenemycolor_a;

                bgr1 = hudscoresplayercolorbg_r;
                bgg1 = hudscoresplayercolorbg_g;
                bgb1 = hudscoresplayercolorbg_b;
                bga1 = hudscoresplayercolorbg_a;

                bgr2 = hudscoresenemycolorbg_r;
                bgg2 = hudscoresenemycolorbg_g;
                bgb2 = hudscoresenemycolorbg_b;
                bga2 = hudscoresenemycolorbg_a;
            }
            else
            {
                int frags=0, frags2=0;
                if(m_teammode) frags = bestgroups[0]->score;
                else frags = bestplayers[0]->frags;
                frags = limitscore(frags);

                if(frags >= 9999)
                    snprintf(buf1, 5, "WIN");
                else
                    snprintf(buf1, 5, "%d", frags);
                text_bounds(buf1, tw1, th1);

                if(m_teammode)
                {
                    loopk(grsz)
                        if(!strcmp(bestgroups[k]->team, currentplayer->team))
                            frags2 = bestgroups[k]->score;
                }
                else
                    frags2 = currentplayer->frags;
                frags2 = limitscore(frags2);

                if(frags2 >= 9999)
                    snprintf(buf2, 5, "WIN");
                else
                    snprintf(buf2, 5, "%d", frags2);
                text_bounds(buf2, tw2, th2);

                r2 = hudscoresplayercolor_r;
                g2 = hudscoresplayercolor_g;
                b2 = hudscoresplayercolor_b;
                a2 = hudscoresplayercolor_a;

                r1 = hudscoresenemycolor_r;
                g1 = hudscoresenemycolor_g;
                b1 = hudscoresenemycolor_b;
                a1 = hudscoresenemycolor_a;

                bgr2 = hudscoresplayercolorbg_r;
                bgg2 = hudscoresplayercolorbg_g;
                bgb2 = hudscoresplayercolorbg_b;
                bga2 = hudscoresplayercolorbg_a;

                bgr1 = hudscoresenemycolorbg_r;
                bgg1 = hudscoresenemycolorbg_g;
                bgb1 = hudscoresenemycolorbg_b;
                bga1 = hudscoresenemycolorbg_a;
            }
            int fw = 0, fh = 0;
            text_bounds("00", fw, fh);
            fw = max(fw, max(tw1, tw2));

            float addoffset = 0.0, offsety = 0.0;
            if(hudscoresoffset_start_x == 1)
                addoffset = 2.0 * fw + 2.0 * borderx + scoresep;
            else if(hudscoresoffset_start_x == 0)
                addoffset = (2.0 * fw + 2.0 * borderx + scoresep)/2.0;

            if(hudscoresvertical)
            {
                addoffset /= 2.0;
                offsety = -fh;
            }
            else
                offsety = -fh/2.0;

            xoff -= addoffset*scorescale;

            drawacoloredquad(xoff/scorescale,
                             yoff/scorescale + offsety,
                             fw + 2.0*borderx,
                             th1,
                             (GLubyte)bgr1,
                             (GLubyte)bgg1,
                             (GLubyte)bgb1,
                             (GLubyte)bga1);
            draw_text(buf1, xoff/scorescale + borderx + (fw-tw1)/2.0,
                      yoff/scorescale + offsety, r1, g1, b1, a1);

            if(!hudscoresvertical)
                xoff += (fw + scoresep)*scorescale;
            else
                offsety = 0;

            drawacoloredquad(xoff/scorescale,
                             yoff/scorescale + offsety,
                             fw + 2.0*borderx,
                             th2,
                             (GLubyte)bgr2,
                             (GLubyte)bgg2,
                             (GLubyte)bgb2,
                             (GLubyte)bga2);
            draw_text(buf2, xoff/scorescale + borderx + (fw-tw2)/2.0,
                      yoff/scorescale + offsety, r2, g2, b2, a2);

            draw_text("", 0, 0, 255, 255, 255, 255);
            glPopMatrix();
        }
    }

    void drawspectator(int w, int h) {
        fpsent *f = followingplayer();
        if(!f || player1->state!=CS_SPECTATOR) return;

        int conw = int(w/staticscale), conh = int(h/staticscale);
        float specscale = (1 + newhud_spectatorsize/10.0)*h/1080.0;
        float xoff = newhud_spectatorpos_x*conw/1000;
        float yoff = newhud_spectatorpos_y*conh/1000;

        glPushMatrix();
        if(newhud)
            glScalef(staticscale*specscale, staticscale*specscale, 1);
        else
            glScalef(h/1800.0f, h/1800.0f, 1);

        draw_text("", 0, 0, 255, 255, 255, 255);
        int pw, ph, tw, th, fw, fh;
        text_bounds("  ", pw, ph);
        text_bounds("SPECTATOR", tw, th);
        th = max(th, ph);
        text_bounds(f ? colorname(f) : " ", fw, fh);
        fh = max(fh, ph);
        if(!newhud)
            draw_text("SPECTATOR", w*1800/h - tw - pw, 1650 - th - fh);

        if(f)
        {
            int color = f->state!=CS_DEAD ? 0xFFFFFF : 0x606060;
            if(f->privilege)
            {
                if(!newhud || !newhud_spectatorsnocolor)
                {
                    color = f->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(f->state==CS_DEAD) color = (color>>1)&0x7F7F7F;
                }
            }
            if(newhud)
            {
                const char *cname;
                int w1=0, h1=0;
                cname = colorname(f);
                text_bounds(cname, w1, h1);
                if(newhud_spectatorpos_start_x == 0)
                    draw_text(cname, xoff/specscale - w1/2.0, yoff/specscale,
                              (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF);
                else if(newhud_spectatorpos_start_x == 1)
                {
                    xoff = (1000 - newhud_spectatorpos_x)*conw/1000;
                    draw_text(cname, xoff/specscale - w1, yoff/specscale,
                              (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF);
                }
                else
                    draw_text(cname, xoff/specscale, yoff/specscale,
                              (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF);
            }
            else
                draw_text(colorname(f), w*1800/h - fw - pw, 1650 - fh, (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF);
        }
        draw_text("", 0, 0, 255, 255, 255, 255);
        glPopMatrix();
    }

    void drawnewhuditems(fpsent *d, int w, int h)
    {
        if((newhud_itemsdisablewithgui && guiisshowing()) ||
           (newhud_itemsdisablewithscoreboard && getvar("scoreboard"))) return;
        if(d->quadmillis)
        {
            char buf[10];
            int conw = int(w/staticscale), conh = int(h/staticscale);
            float itemsscale = (1 + newhud_itemssize/10.0)*h/1080.0;
            float xoff = newhud_itemspos_reverse_x ? (1000 - newhud_itemspos_x)*conw/1000 : newhud_itemspos_x*conw/1000;
            float yoff = newhud_itemspos_y*conh/1000;

            glPushMatrix();
            snprintf(buf, 10, "M");
            int tw = 0, th = 0, adj = 0;
            text_bounds(buf, tw, th);
            glScalef(staticscale*itemsscale, staticscale*itemsscale, 1);
            draw_text("", 0, 0, 255, 255, 255, 255);
            if(newhud_itemspos_reverse_x)
                adj = newhud_itemspos_centerfirst ? -th/2 : -th;
            else
                adj = newhud_itemspos_centerfirst ? -th/2 : 0;
            drawicon(HICON_QUAD, xoff/itemsscale + adj, yoff/itemsscale - th/2.0, th);
            glPopMatrix();
        }
    }

    void drawlagmeter(int w, int h)
    {
        fpsent *d = (player1->state == CS_SPECTATOR) ? followingplayer() : player1;
        if(!d || (lagometerdisablewithgui && guiisshowing()) || (lagometerdisablewithscoreboard && getvar("scoreboard")) ||
           (lagometerdisablelocal && !connectedpeer() && !demoplayback)) return;

        int conw = int(w/staticscale), conh = int(h/staticscale);

        float xoff = lagometerpos_start_x == 1 ? (1000-lagometerpos_x)*conw/1000 : lagometerpos_x*conw/1000;
        float yoff = lagometerpos_y*conh/1000;
        float xpsz = lagometerlen/staticscale;
        float ypsz = lagometerheight/staticscale;

        glPushMatrix();
        glScalef(staticscale, staticscale, 1);
        if(lagometerpos_start_x == 1)
            xoff -= lagometerlen*lagometercolsz/staticscale;
        else if(lagometerpos_start_x == 0)
            xoff -= lagometerlen*lagometercolsz/staticscale/2.0;
        yoff -= ypsz/2.0;
        if(!(d == player1 && lagometeronlypingself))
        {
            if(!lagometernobg)
                drawacoloredquad(xoff,
                                 yoff,
                                 xpsz*lagometercolsz,
                                 ypsz,
                                 (GLubyte)255,
                                 (GLubyte)255,
                                 (GLubyte)255,
                                 (GLubyte)50);
            float ch = 0.0;
            int len = min(d->lagdata.ping.len, lagometerlen),
                ping;
            loopi(len)
            {
                ch = min(100, d->lagdata.ping[i])/staticscale * lagometerheight/100.0;
                ping = d->lagdata.ping[i];
                drawacoloredquad(xoff + (i*lagometercolsz)/staticscale,
                                 yoff + ypsz - ch,
                                 lagometercolsz/staticscale,
                                 ch,
                                 ping >= 100 ? (GLubyte)255 : (GLubyte)0,
                                 (GLubyte)0,
                                 ping >= 100 ? (GLubyte)0 : (GLubyte)255,
                                 (GLubyte)127);
            }
        }
        #define LAGOMETERBUFSZ 10
        if(lagometershowping)
        {
            char buf[LAGOMETERBUFSZ];
            glPushMatrix();
            float scale = lagometerpingsz/100.0;
            glScalef(scale, scale, 1);
            int w1=0, h1=0;
            int pl = getpacketloss();
            bool coloredpl = false;
            if(pl && d == player1)
            {
                snprintf(buf, LAGOMETERBUFSZ, "%d %d", (int)d->ping, pl);
                coloredpl = true;
            }
            else
                snprintf(buf, LAGOMETERBUFSZ, "%d", (int)d->ping);
            text_bounds(buf, w1, h1);
            if(d == player1 && lagometeronlypingself)
            {
                int gap = 0;
                if(lagometerpos_start_x == 1)
                    gap = xpsz*lagometercolsz/scale - w1;
                else if(lagometerpos_start_x == 0)
                    gap = xpsz*lagometercolsz/scale - w1/2.0;
                draw_text(buf, xoff/scale + gap,
                          (yoff + ypsz/2.0)/scale - h1/2,
                          255, coloredpl ? 0 : 255, coloredpl ? 0 : 255, 255);
            }
            else
                draw_text(buf, (xoff + xpsz*lagometercolsz)/scale - w1 - h1/4 ,
                          yoff/scale,
                          255, coloredpl ? 0 : 255, coloredpl ? 0 : 255, 255);
            glPopMatrix();
        }
        glPopMatrix();
    }

    void drawspeedmeter(int w, int h)
    {
        fpsent *d = (player1->state == CS_SPECTATOR) ? followingplayer() : player1;
        if(!d || (speedometerdisablewithgui && guiisshowing()) || (speedometerdisablewithscoreboard && getvar("scoreboard"))) return;

        int conw = int(w/staticscale), conh = int(h/staticscale);

        float xoff = speedometerpos_start_x == 1 ? (1000-speedometerpos_x)*conw/1000 : speedometerpos_x*conw/1000,
              yoff = speedometerpos_y*conh/1000,
              xpsz = speedometerlen/staticscale,
              ypsz = speedometerheight/staticscale;

        glPushMatrix();
        glScalef(staticscale, staticscale, 1);
        if(speedometerpos_start_x == 1)
            xoff -= speedometerlen*speedometercolsz/staticscale;
        else if(speedometerpos_start_x == 0)
            xoff -= speedometerlen*speedometercolsz/staticscale/2.0;
        yoff -= ypsz/2.0;
        if(!(d == player1 && speedometeronlyspeedself))
        {
            if(!speedometernobg)
                drawacoloredquad(xoff,
                                 yoff,
                                 xpsz*speedometercolsz,
                                 ypsz,
                                 (GLubyte)255,
                                 (GLubyte)255,
                                 (GLubyte)255,
                                 (GLubyte)50);
            float ch = 0.0;
            int len = min(d->speeddata.speed.len, speedometerlen),
                speed;
            loopi(len)
            {
                ch = min(170, d->speeddata.speed[i])/staticscale * speedometerheight/170.0;
                speed = d->speeddata.speed[i];
                drawacoloredquad(xoff + (i*speedometercolsz)/staticscale,
                                 yoff + ypsz - ch,
                                 speedometercolsz/staticscale,
                                 ch,
                                 speed >= 170 ? (GLubyte)255 : (GLubyte)0,
                                 (GLubyte)0,
                                 speed >= 170 ? (GLubyte)0 : (GLubyte)255,
                                 (GLubyte)127);
            }
        }
        #define SPEEDOMETERBUFSZ 10
        if(speedometershowspeed)
        {
            char buf[SPEEDOMETERBUFSZ];
            glPushMatrix();
            float scale = speedometerspeedsz/100.0;
            glScalef(scale, scale, 1);
            int w1=0, h1=0;
            snprintf(buf, SPEEDOMETERBUFSZ, "%d", (int)(d->state!=CS_DEAD ? d->vel.magnitude2() : 0));
            text_bounds(buf, w1, h1);
            if(d == player1 && speedometeronlyspeedself)
            {
                int gap = 0;
                if(speedometerpos_start_x == 1)
                    gap = xpsz*speedometercolsz/scale - w1;
                else if(speedometerpos_start_x == 0)
                    gap = xpsz*speedometercolsz/scale - w1/2.0;
                draw_text(buf, xoff/scale + gap,
                          (yoff + ypsz/2.0)/scale - h1/2,
                          255, 255, 255, 255);
            }
            else
                draw_text(buf, (xoff + xpsz*speedometercolsz)/scale - w1 - h1/4 ,
                          yoff/scale,
                          255, 255, 255, 255);
            glPopMatrix();
        }
        glPopMatrix();
    }

    void drawsehud(int w, int h, fpsent *d)
    {
        if(sehuddisableininsta && m_insta) return;

        glPushMatrix();
        glScalef(1/1.2f, 1/1.2f, 1);

        if(!m_insta) draw_textf("%d", 80, h*1.2f-136, max(0, d->health));
        defformatstring(ammo)("%d", player1->ammo[d->gunselect]);
        int wb, hb;
        text_bounds(ammo, wb, hb);
        draw_textf("%d", w*1.2f-wb-80, h*1.2f-136, d->ammo[d->gunselect]);

        if(sehudshowitems)
        {
            if(d->quadmillis)
            {
                settexture("packages/hud/se_hud_quaddamage_left.png");
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(0,   h*1.2f-207);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(539, h*1.2f-207);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(539, h*1.2f);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(0,   h*1.2f);
                glEnd();

                settexture("packages/hud/se_hud_quaddamage_right.png");
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(w*1.2f-135, h*1.2f-207);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(w*1.2f,     h*1.2f-207);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(w*1.2f,     h*1.2f);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(w*1.2f-135, h*1.2f);
                glEnd();
            }
        }

        if(sehudshowhealth)
        {
            if(d->maxhealth > 100)
            {
                settexture("packages/hud/se_hud_megahealth.png");
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(0,   h*1.2f-207);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(539, h*1.2f-207);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(539, h*1.2f);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(0,   h*1.2f);
                glEnd();
            }

            int health = (d->health*100)/d->maxhealth,
                hh = (health*101)/100;
            float hs = (health*1.0f)/100;

            if(d->health > 0 && !m_insta)
            {
                settexture("packages/hud/se_hud_health.png");
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 1.0f-hs); glVertex2f(47, h*1.2f-hh-56);
                glTexCoord2f(1.0f, 1.0f-hs); glVertex2f(97, h*1.2f-hh-56);
                glTexCoord2f(1.0f, 1.0f);    glVertex2f(97, h*1.2f-57);
                glTexCoord2f(0.0f, 1.0f);    glVertex2f(47, h*1.2f-57);
                glEnd();
            }
        }

        if(sehudshowarmour)
        {
            int armour = (d->armour*100)/200,
                ah = (armour*167)/100;
            float as = (armour*1.0f)/100;

            if(d->armour > 0)
            {
                settexture("packages/hud/se_hud_armour.png");
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(130,    h*1.2f-62);
                glTexCoord2f(as,   0.0f); glVertex2f(130+ah, h*1.2f-62);
                glTexCoord2f(as,   1.0f); glVertex2f(130+ah, h*1.2f-44);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(130,    h*1.2f-44);
                glEnd();
            }
        }

        if(!m_insta)
        {
            settexture("packages/hud/se_hud_left.png");
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(0,   h*1.2f-207);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(539, h*1.2f-207);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(539, h*1.2f);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(0,   h*1.2f);
            glEnd();
        }

        if(sehudshowammo)
        {
            settexture("packages/hud/se_hud_right.png");
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(w*1.2f-135, h*1.2f-207);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(w*1.2f,     h*1.2f-207);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(w*1.2f,     h*1.2f);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(w*1.2f-135, h*1.2f);
            glEnd();

            int maxammo = 1;

            switch(d->gunselect)
            {
                case GUN_FIST:
                    maxammo = 1;
                    break;
                case GUN_RL:
                case GUN_RIFLE:
                    maxammo = m_insta ? 100 : 15;
                    break;
                case GUN_SG:
                case GUN_GL:
                    maxammo = 30;
                    break;
                case GUN_CG:
                    maxammo = 60;
                    break;
                case GUN_PISTOL:
                    maxammo = 120;
                    break;
            }

            int curammo = min((d->ammo[d->gunselect]*100)/maxammo, maxammo),
            amh = (curammo*101)/100;

            float ams = (curammo*1.0f)/100;

            if(d->ammo[d->gunselect] > 0)
            {
                settexture("packages/hud/se_hud_health.png");
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 1.0f-ams); glVertex2f(w*1.2f-47, h*1.2f-amh-56);
                glTexCoord2f(1.0f, 1.0f-ams); glVertex2f(w*1.2f-97, h*1.2f-amh-56);
                glTexCoord2f(1.0f, 1.0f);     glVertex2f(w*1.2f-97, h*1.2f-57);
                glTexCoord2f(0.0f, 1.0f);     glVertex2f(w*1.2f-47, h*1.2f-57);
                glEnd();
            }

            glPopMatrix();
            glPushMatrix();
            glScalef(1/4.0f, 1/4.0f, 1);
            defformatstring(icon)("packages/hud/se_hud_gun_%d.png", d->gunselect);
            settexture(icon);
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(w*4.0f-1162,    h*4.0f-350);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(w*4.0f-650,     h*4.0f-350);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(w*4.0f-650,     h*4.0f-50);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(w*4.0f-1162,    h*4.0f-50);
            glEnd();
        }
        glPopMatrix();
    }

    void drawhudline(int w, int h)
    {
        if((guiisshowing() && hudlinedisablewithgui) || (getvar("scoreboard") && hudlinedisablewithscoreboard)) return;

        glPushMatrix();
        glScalef(h/1800.0f, h/1800.0f, 1);

        if(isclanwar(true, hudlinecolor) || isduel(true, hudlinecolor))
        {
            int tw, th;
            int posx, posy;

            text_bounds(battleheadline, tw, th);
            posx = w*900/h-tw/2;
            posy = player1->state==CS_SPECTATOR ? 2*th : 0;

            glPushMatrix();
            glTranslatef(posx, posy, 0);
            glScalef(hudlinescale*0.1f, hudlinescale*0.1f, 1);
            glTranslatef(-posx, -posy, 0);

            draw_text(battleheadline, posx, posy);
            glPopMatrix();
        }
        else if(hudlineminiscoreboard)
        {
            fpsent *f = followingplayer();
            if(!f) f = player1;
            fpsent *first = NULL;
            loopv(players)
            {
                fpsent *p = players[i];
                if(!p || p->state==CS_SPECTATOR) continue;
                if(!first) first = p;
                else if(p->frags > first->frags || (p->frags==first->frags && p->deaths < first->deaths)) first = p;
            }

            int posx = w*900/h;
            int posy = player1->state==CS_SPECTATOR;

            glPushMatrix();
            glTranslatef(posx, posy, 0);
            glScalef(hudlinescale*0.1f, hudlinescale*0.1f, 1);
            glTranslatef(-posx, -posy, 0);

            if(m_teammode)
            {
                int tw, th;
                const char *winstatus = ownteamstatus();

                if(winstatus)
                {
                    text_bounds(winstatus, tw, th);
                    draw_textf("%s", posx-tw/2, posy, winstatus);
                    posy += th;
                }
            }
            if(first)
            {
                int tw2, th2;
                defformatstring(buf)("1. \fs\f%d%s\fr %d", isteam(first->team, player1->team) ? 1 : 3, first->name, first->frags);
                text_bounds(buf, tw2, th2);
                draw_textf(buf, posx-tw2/2, posy);
            }

            glPopMatrix();
        }
        glPopMatrix();
    }

    void drawhudelements(int w, int h)
    {
        glPushMatrix();
        glScalef(h/1800.0f, h/1800.0f, 1);

        loopv(hudelements)
        {
            glPushMatrix();
            switch(hudelements[i]->type)
            {
                case 0:
                {
                    char *text = executestr(hudelements[i]->script);
                    if(text)
                    {
                        if(text[0])
                        {
                            glScalef(hudelements[i]->xscale, hudelements[i]->yscale, 1);
                            draw_text(text, hudelements[i]->xpos/hudelements[i]->xscale, hudelements[i]->ypos/hudelements[i]->yscale);
                        }
                        DELETEA(text);
                    }
                    break;
                }
                case 1:
                {
                    if(hudelements[i]->script)
                    {
                        glScalef(hudelements[i]->xscale, hudelements[i]->yscale, 1);
                        int id = atoi(hudelements[i]->script);
                        drawicon(id, hudelements[i]->xpos/hudelements[i]->xscale, hudelements[i]->ypos/hudelements[i]->yscale);
                    }
                    break;
                }
                case 2:
                {
                    if(hudelements[i]->script)
                    {
                        defformatstring(fname)("cache/%s", hudelements[i]->script);
                        drawimage(fname, hudelements[i]->xpos, hudelements[i]->ypos, hudelements[i]->xscale, hudelements[i]->yscale);
                    }
                    break;
                }
            }
            glPopMatrix();
        }

        glPopMatrix();
    }

    void drawhudstats(int w, int h, fpsent *d)
    {
        if((guiisshowing() && hudstatsdisablewithgui) || (getvar("scoreboard") && hudstatsdisablewithscoreboard))
            return;
        glPushMatrix();
        glScalef(h/1800.0f, h/1800.0f, 1);

        float speed = (float)(d->state!=CS_DEAD ? (float)d->vel.magnitude2() : 0);
        float kpd = (float)(d->frags/(float)max(d->deaths, 1));
        float acc = (float)(d->totaldamage/(float)max(d->totalshots, 1))*100.0f;

        draw_textfa("\f%dPing:\nSpeed:\n\n%sFrags:\nK/D:\nDeaths:\nAccuracy:",
                    hudstatsalpha,
                    hudstatsoffx,
                    hudstatsoffy,
                    hudstatscolor,
                    cmode ? m_collect ? "Skulls:\n" : "Flags:\n" : "");
        draw_textfa("\f%d%.1f\n%.1f\n\n%s%d\n%.1f\n%d\n%.1f%%",
                    hudstatsalpha,
                    hudstatsoffx + 290,
                    hudstatsoffy,
                    hudstatscolor,
                    d->ping,
                    speed,
                    cmode ? (tempformatstring("%d\n", d->flags)) : "",
                    d->frags,
                    kpd,
                    d->deaths,
                    acc);

        glPopMatrix();
    }

    static const char *COLOR_ALIVE = "\f0";
    static const char *COLOR_DEAD = "\f4";
    static const char *COLOR_ME = "\f7";
    static const char *COLOR_SPEC = "\f7";
    static const char *COLOR_UNKNOWN = "\f7";

    static const char *specteam = "spectators";
    static const char *noteams = "no teams";
    static const int maxteams = 10;

    struct playerteam
    {
        bool needfrags;
        int frags;
        int score;
        const char *name;
        vector<fpsent*> players;
    };

    static int compareplayer(const fpsent *a, const fpsent *b)
    {
        if(a->frags > b->frags) return 1;
        else if(a->frags < b->frags) return 0;
        else return !strcmp(a->name, b->name) ? 1 : 0;
    }

    static int numteams = 0;
    static playerteam teams[maxteams];
    static vector<teamscore> teamscores;

    static void sortteams(playerteam **result, bool hidefrags)
    {
        loopi(numteams) result[i] = &teams[i];

        if(numteams <= 1)
            return;

        loopi(numteams)
        {
            loopj(numteams-i-1)
            {
                playerteam *a = result[j];
                playerteam *b = result[j+1];
                if((hidefrags ? a->score < b->score : a->frags < b->frags) || a->name == specteam)
                {
                    result[j] = b;
                    result[j+1] = a;
                }
            }
        }
    }

    static playerteam *accessteam(const char *teamname, bool hidefrags, bool add = false)
    {
        if(!add) loopi(numteams) if(!strcmp(teams[i].name, teamname)) return &teams[i];
        if(numteams < maxteams)
        {
            playerteam &team = teams[numteams];
            team.name = teamname;
            teaminfo *ti = getteaminfo(teamname);
            if(ti) team.frags = ti->frags;
            else team.frags = 0;
            team.score = 0;
            if(hidefrags)
            {
                team.needfrags = !team.frags;
                loopv(teamscores) if(!strcmp(teamscores[i].team, teamname))
                {
                    team.score = teamscores[i].score;
                    break;
                }
            }
            else team.needfrags = !ti;
            team.players.setsize(0);
            numteams++;
            return &team;
        }
        return NULL;
    }

    static inline int rescalewidth(int v, int w)
    {
        static int o = 1024;
        if (w == o) return v;
        float tmp = v; tmp /= o; tmp *= w;
        return tmp;
    }

    static inline int rescaleheight(int v, int h)
    {
        static int o = 640;
        if (h == o) return v;
        float tmp = v; tmp /= o; tmp *= h;
        return tmp;
    }

    extern double getweaponaccuracy(int gun, fpsent *f = NULL);

    void renderplayerdisplay(int conw, int conh, int fonth, int w, int h)
    {
        if(!showplayerdisplay ||
            (m_edit || getvar("scoreboard") || !isconnected(false, true)))
             return;

        if(fullconsole || w < 1024 || h < 640)
            return;

        static const int lw[] = { -700, -1000 };

        glPushMatrix();
        glScalef(fontscale, fontscale, 1);

        if(!demoplayback)
            clients.add(player1);

        numteams = 0;
        teamscores.setsize(0);

        bool hidefrags = m_teammode && cmode && cmode->hidefrags();

        if(hidefrags)
            cmode->getteamscores(teamscores);

        loopvrev(clients)
        {
            fpsent *d = clients[i];

            if(!d || (d->state == CS_SPECTATOR && !playerdisplayshowspectators))
                continue;

            playerteam *team;
            const char *teamname = d->state == CS_SPECTATOR ? specteam : m_teammode ? d->team : "";

            team = accessteam(teamname, hidefrags);

            if(!team)
            {
                if(!m_teammode && numteams)
                {
                    if(teamname == specteam)
                    {
                        loopi(numteams) if(teams[i].name == specteam)
                        {
                            team = &teams[i];
                            break;
                        }
                    }
                    else
                    {
                        loopi(numteams) if(teams[i].name != specteam)
                        {
                            team = &teams[i];
                            break;
                        }
                    }
                }

                if(!team)
                {
                    team = accessteam(teamname, hidefrags, true);

                    if(!team)
                        break;
                }
            }

            team->players.add(d);
            if(team->needfrags) team->frags += d->frags;
        }

        if(!demoplayback)
            clients.pop(); // pop player1

        loopi(numteams)
            teams[i].players.sort(compareplayer);

        static playerteam *teams[maxteams];
        sortteams(teams, hidefrags);

        int j = 0, playersshown = 0;
        numteams = min(playerdisplaymaxteams, numteams);

        loopi(numteams)
        {
            playerteam *team = teams[i];

            if(i > 0) j += 2;

            int left;
            int top = conh;
            top += rescaleheight(-100+LINEOFFSET(j)+
                                 playerdisplaylineoffset*j+playerdisplayheightoffset+
                                 (m_ctf ? -50 : 0), h);

            const char *teamname = team->name;

            if(!m_teammode && teamname != specteam)
                teamname = noteams;

            static draw dt;

            dt.clear();
            dt.setalpha(playerdisplayalpha);

            left = conw+rescalewidth(lw[1]+playerdisplaywidthoffset, w);
            dt << teamname;
            dt.drawtext(left, top);

            if(team->name != specteam)
            {
                dt.cleartext();
                left = conw+rescalewidth(lw[0]+playerdisplaywidthoffset+playerdisplayrightoffset, w);

                if(hidefrags)
                    dt << "(" << team->frags << " / " << team->score << ")";
                else
                    dt << "(" << team->frags << ")";

                dt.drawtext(left, top);
            }

            int teamlimit = playerdisplayplayerlimit/numteams;

            loopv(team->players)
            {
                fpsent *d = team->players[i];

                if(!d)
                    continue;

                j++;
                playersshown++;

                const char *color;

                if(d != player1)
                {
                    switch (d->state)
                    {
                        case CS_ALIVE: color = COLOR_ALIVE; break;
                        case CS_DEAD: color = COLOR_DEAD; break;
                        case CS_SPECTATOR: color = COLOR_SPEC; break;
                        default: color = COLOR_UNKNOWN;
                    }
                }
                else color = COLOR_ME;

                int c = d->name[playerdisplaymaxnamelen];
                d->name[playerdisplaymaxnamelen] = 0;

                int top = conh;

                top += rescaleheight(-100+LINEOFFSET(j)+
                                     playerdisplaylineoffset*j+
                                     playerdisplayheightoffset+(m_ctf ? -50 : 0), h);

                dt.cleartext();
                left = conw+rescalewidth(lw[1]+playerdisplaywidthoffset, w);

                dt << color << d->name;
                dt.drawtext(left, top);

                dt.cleartext();
                left = conw+rescalewidth(lw[0]+playerdisplaywidthoffset+playerdisplayrightoffset, w);

                dt << color << "(" << d->frags << " / " << (float)getweaponaccuracy(-1, d) << "%)";

                dt.drawtext(left, top);

                d->name[playerdisplaymaxnamelen] = c;

                if(playersshown >= playerdisplayplayerlimit)
                    goto retn;

                if(!--teamlimit)
                    break;
            }
        }

        retn:;
        glPopMatrix();
    }

    void gameplayhud(int w, int h)
    {
        if(player1->state==CS_SPECTATOR &&
           !(newhud && newhud_spectatorsdisablewithgui && guiisshowing()) &&
           !(newhud && newhud_spectatorsdisablewithscoreboard && getvar("scoreboard")))
            drawspectator(w, h);

        fpsent *d = hudplayer();
        if(d->state!=CS_EDITING)
        {
            if(newhud)
            {
                if(d->state!=CS_SPECTATOR)
                {
                    drawnewhudhp(d, w, h);
                    drawnewhudammo(d, w, h);
                    drawnewhuditems(d, w, h);
                }
            }
            else if(d->state!=CS_SPECTATOR) drawhudicons(d, w, h);

            glPushMatrix();
            glScalef(h/1800.0f, h/1800.0f, 1);
            if(cmode) cmode->drawhud(d, w, h);
            glPopMatrix();
        }

        if(ammobar && !m_edit && d->state!=CS_DEAD && d->state!=CS_SPECTATOR &&
           !(ammobardisablewithgui && guiisshowing()) &&
           !(ammobardisablewithscoreboard && getvar("scoreboard")) &&
           !(m_insta && ammobardisableininsta))
            drawammobar(d, w, h);

        if(gameclock && !m_edit && !(gameclockdisablewithgui && guiisshowing()) && !(gameclockdisablewithscoreboard && getvar("scoreboard")))
            drawclock(w, h);

        if(hudscores && !m_edit && !(hudscoresdisablewithgui && guiisshowing()) && !(hudscoresdisablewithscoreboard && getvar("scoreboard")))
            drawscores(w, h);

        if(lagometer)
            drawlagmeter(w, h);

        if(speedometer)
            drawspeedmeter(w, h);

        if(sehud)
            drawsehud(w, h, d);

        if(hudline)
            drawhudline(w, h);

        drawhudelements(w, h);

        if(hudstats)
            drawhudstats(w, h, d);

        if(fragmsg && !(guiisshowing() && fragmsgdisablewithgui) && !(getvar("scoreboard") && fragmsgdisablewithscoreboard) && (d->lastvictim != NULL || d->lastkiller != NULL) & (lastmillis-d->lastfragtime < fragmsgfade + 255 || lastmillis-d->lastdeathtime < fragmsgfade + 255))
            drawfragmsg(d, w, h);

        renderplayerdisplay(w, h, FONTH, w, h);
    }

    int clipconsole(int w, int h)
    {
        if(cmode) return cmode->clipconsole(w, h);
        return 0;
    }

    VARP(teamcrosshair, 0, 1, 1);
    VARP(hitcrosshair, 0, 425, 1000);

    VARP(crosshaircolor, 0, 0xFFFFFF, 0xFFFFFF);

    const char *defaultcrosshair(int index)
    {
        switch(index)
        {
            case 2: return "data/hit.png";
            case 1: return "data/teammate.png";
            default: return "data/crosshair.png";
        }
    }

    VARP(crosshairinfo, 0, 1, 1);
    VARP(crosshairinfoenemys, 0, 1, 1);

    int selectcrosshair(float &r, float &g, float &b, int &w, int &h)
    {
        fpsent *d = hudplayer();
        if(d->state==CS_SPECTATOR || d->state==CS_DEAD || intermission) return -1;

        r = ((crosshaircolor>>16)&0xFF)/255.0f;
        g = ((crosshaircolor>>8)&0xFF)/255.0f;
        b = ((crosshaircolor)&0xFF)/255.0f;

        int crosshair = 0;
        dynent *o = intersectclosest(d->o, worldpos, d);
        if(lasthit && lastmillis - lasthit < hitcrosshair) crosshair = 2;
        else if(teamcrosshair)
        {
            if(o && o->type==ENT_PLAYER && isteam(((fpsent *)o)->team, d->team))
            {
                crosshair = 1;
                r = g = 0;
            }
        }

        if(o && o->type==ENT_PLAYER && crosshairinfo)
        {
            if(isteam(((fpsent *)o)->team, d->team)) draw_text(((fpsent *)o)->name, w/2, h/3, 64, 64, 255);
            else if(crosshairinfoenemys && (m_teammode|| teamskins)) draw_text(((fpsent *)o)->name, w/2, h/3, 255, 64, 64);
            else if(crosshairinfoenemys) draw_text(((fpsent *)o)->name, w/2, h/3, 64, 255, 64);
        }

        if(crosshair!=1 && !editmode && !m_insta)
        {
            if(d->health<=25) { r = 1.0f; g = b = 0; }
            else if(d->health<=50) { r = 1.0f; g = 0.5f; b = 0; }
        }
        if(d->gunwait) { r *= 0.5f; g *= 0.5f; b *= 0.5f; }
        return crosshair;
    }

    void lighteffects(dynent *e, vec &color, vec &dir)
    {
#if 0
        fpsent *d = (fpsent *)e;
        if(d->state!=CS_DEAD && d->quadmillis)
        {
            float t = 0.5f + 0.5f*sinf(2*M_PI*lastmillis/1000.0f);
            color.y = color.y*(1-t) + t;
        }
#endif
    }

    bool sortupsidedown;
    int sortby = SB_NOTHING;

    bool serverinfostartcolumn(g3d_gui *g, int i)
    {
        static const char * const names[] = { "Ping ", "Players ", "Mode ", "Map ", "Time ", "Master ", "Host ", "Port ", "Description " };
        static const float struts[] =       { 7,       7,          12.5f,   14,      7,      8,         14,      7,       24.5f };
        if(size_t(i) >= sizeof(names)/sizeof(names[0])) return false;
        g->pushlist();
        if(g->buttonf("%s", sortby==i ? (sortupsidedown ? 0xBB0000 : 0x00BB00) : 0x0000BB, !i && sortby!=i ? " " : sortby==i ? sortupsidedown ? "arrow_up" : "arrow_down" : NULL, NULL, names[i])&G3D_UP)
        {
            if(sortby==i)
            {
                if(sortupsidedown)
                {
                    sortupsidedown = false;
                    sortby = SB_NOTHING;
                }
                else sortupsidedown = true;
            }
            else
            {
                sortby = i;
                sortupsidedown = false;
            }
        }
        if(struts[i]) g->strut(struts[i]);
        g->mergehits(true);
        return true;
    }

    void serverinfoendcolumn(g3d_gui *g, int i)
    {
        g->mergehits(false);
        g->column(i);
        g->poplist();
    }

    const char *mastermodecolor(int n, const char *unknown)
    {
        return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodecolors)/sizeof(mastermodecolors[0])) ? mastermodecolors[n-MM_START] : unknown;
    }

    const char *mastermodeicon(int n, const char *unknown)
    {
        return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodeicons)/sizeof(mastermodeicons[0])) ? mastermodeicons[n-MM_START] : unknown;
    }

    bool serverinfoentry(g3d_gui *g, int i, const char *name, int port, const char *sdesc, const char *map, int ping, const vector<int> &attr, int np)//, const char *country)
    {
        //defformatstring(countryicon)("%s.png", country);
        if(ping < 0 || attr.empty() || attr[0]!=PROTOCOL_VERSION)
        {
            switch(i)
            {
                case 0:
                    if(g->button(" ", 0xFFFFDD, "serverunk")&G3D_UP) return true;
                    break;

                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                    if(g->button(" ", 0xFFFFDD)&G3D_UP) return true;
                    break;

                case 6:
                    if(g->buttonf("%s ", 0xFFFFDD, NULL, NULL, name)&G3D_UP) return true;
                    break;

                case 7:
                    if(g->buttonf("%d ", 0xFFFFDD, NULL, NULL, port)&G3D_UP) return true;
                    break;

                case 8:
                    if(ping < 0)
                    {
                        if(g->button(sdesc, 0xFFFFDD)&G3D_UP) return true;
                    }
                    else if(g->buttonf("[%s protocol] ", 0xFFFFDD, NULL, NULL, attr.empty() ? "unknown" : (attr[0] < PROTOCOL_VERSION ? "older" : "newer"))&G3D_UP) return true;
                    break;
                /*case 9:
                    if(g->buttonf("%s ", 0xFFFFDD, countryicon, NULL, country)&G3D_UP) return true;
                    break;*/
            }
            return false;
        }

        switch(i)
        {
            case 0:
            {
                const char *icon = attr.inrange(3) && np >= attr[3] ? "serverfull" : (attr.inrange(4) ? mastermodeicon(attr[4], "serverunk") : "serverunk");
                if(g->buttonf("%d ", 0xFFFFDD, icon, NULL, ping)&G3D_UP) return true;
                break;
            }

            case 1:
                if(attr.length()>=4)
                {
                    if(g->buttonf(np >= attr[3] ? "\f3%d/%d " : "%d/%d ", 0xFFFFDD, NULL, NULL, np, attr[3])&G3D_UP) return true;
                }
                else if(g->buttonf("%d ", 0xFFFFDD, NULL, NULL, np)&G3D_UP) return true;
                break;

            case 2:
                if(g->buttonf("%s ", 0xFFFFDD, NULL, NULL, attr.length()>=2 ? server::prettymodename(attr[1], "") : "")&G3D_UP) return true;
                break;

            case 3:
                if(g->buttonf("%.25s ", 0xFFFFDD, NULL, NULL, map)&G3D_UP) return true;
                break;

            case 4:
                if(attr.length()>=3 && attr[2] > 0)
                {
                    int secs = clamp(attr[2], 0, 59*60+59),
                        mins = secs/60;
                    secs %= 60;
                    if(g->buttonf("%d:%02d ", 0xFFFFDD, NULL, NULL, mins, secs)&G3D_UP) return true;
                }
                else if(g->buttonf(" ", 0xFFFFDD)&G3D_UP) return true;
                break;
            case 5:
                if(g->buttonf("%s%s ", 0xFFFFDD, NULL, NULL, attr.length()>=5 ? mastermodecolor(attr[4], "") : "", attr.length()>=5 ? server::mastermodename(attr[4], "") : "")&G3D_UP) return true;
                break;

            case 6:
                if(g->buttonf("%s ", 0xFFFFDD, NULL, NULL, name)&G3D_UP) return true;
                break;

            case 7:
                if(g->buttonf("%d ", 0xFFFFDD, NULL, NULL, port)&G3D_UP) return true;
                break;

            case 8:
                if(g->buttonf("%.25s", 0xFFFFDD, NULL, NULL, sdesc)&G3D_UP) return true;
                break;
            /*case 9:
                if(g->buttonf("%s ", 0xFFFFDD, countryicon, NULL, country)&G3D_UP) return true;
                break;*/
        }
        return false;
    }

    // any data written into this vector will get saved with the map data. Must take care to do own versioning, and endianess if applicable. Will not get called when loading maps from other games, so provide defaults.
    void writegamedata(vector<char> &extras) {}
    void readgamedata(vector<char> &extras) {}

    const char *savedconfig() { return "saved/config.cfg"; }
    const char *restoreconfig() { return "saved/restore.cfg"; }
    const char *defaultconfig() { return "data/defaults.cfg"; }
    const char *autoexec() { return "saved/autoexec.cfg"; }
    const char *savedservers() { return "saved/servers.cfg"; }
    const char *savedclantags() { return "saved/clantags.cfg"; }
    const char *savedfriends() { return "saved/friends.cfg"; }

    const char **getgamescripts() { return game_scripts; }

    void loadconfigs()
    {
        execfile("auth.cfg", false);
    }

    void saveclantagscfg()
    {
        stream *f = openutf8file(path(savedclantags(), true), "w");
        if(!f) return;

        f->putline("// clantags:");
        f->putline("clearclans");
        loopv(clantags) if(clantags[i]) f->printf("addclan \"%s\"\n", clantags[i]->name);
        delete f;
    }

    void savefriendscfg()
    {
        stream *f = openutf8file(path(savedfriends(), true), "w");
        if(!f) return;

        f->putline("// friends:");
        loopv(friends) if(friends[i]) f->printf("addfriend \"%s\"\n", friends[i]->name);
        delete f;
    }

    void concatgamedesc(char *name, size_t maxlen)
    {
        string buf;
        time_t rawtime;
        tm *timeinfo;

        time(&rawtime);
        timeinfo = localtime(&rawtime);
        strftime(buf, sizeof(buf), "%Y-%m-%d-%H-%M-%S__", timeinfo);
        concatstring(name, buf, maxlen);

        if(!remote) concatstring(name, "local", maxlen);
        else
        {
            if(servinfo[0])
            {
                copystring(buf, servinfo);
                filtertext(buf, buf);
                char *ch = buf;
                while((ch = strpbrk(ch, " /\\:?<>\"|*"))) *ch = '_';
                concatstring(name, buf, maxlen);
            }
        }
    }
}
