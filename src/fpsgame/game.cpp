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
            drawcc(),
            resetphysics();
VAR(deathcamerastate, 0, 0, 1);
VARP(deathcamera, 0, 0, 1);

int lasttimeupdate = 0;

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
    VARP(gunwaithud, 0, 1, 1);
    FVARP(gunwaithudscale, 0, 1.5f, 100.0f);
    VARP(gunwaithudoffsetx, -400, 0, 400);

    void drawhudbody(float x, float y, float sx, float sy, float ty)
    {
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, ty); glVertex2f(x    ,    y);
        glTexCoord2f(1, ty); glVertex2f(x+sx,    y);
        glTexCoord2f(0, 1);  glVertex2f(x    ,    y+sy);
        glTexCoord2f(1, 1);  glVertex2f(x+sx,    y+sy);
        glEnd();
    }

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

        if(gunwaithud)
        {

            glPushMatrix();
            glScalef(h/1800.0f, h/1800.0f, 1);

            float mwait = ((float)(lastmillis-d->lastaction)*(float)(lastmillis-d->lastaction))/((float)d->gunwait*(float)d->gunwait);
            mwait = clamp(mwait, 0.f, 1.f);

            static Texture *gunwaitft = NULL;
            if (!gunwaitft) gunwaitft = textureload("packages/hud/gunwait_filled.png", 0, true, false);
            int size = 128*gunwaithudscale;

            float rw = ((float)w/((float)h/1800.f)), rh = 1800;
            float scale = 1.2f*1.6f;
            float x = (rw/scale)/2.f+gunwaithudoffsetx, y = (rh-size*2.f)/(scale*2.f);

            if(mwait==1.0f) goto dontdraw;

            glPushMatrix();
            glScalef(scale, scale, 1);
            glColor4f(1.0f, 1.0f, 1.0f, 0.5);

            glBindTexture(GL_TEXTURE_2D, gunwaitft->id);

            glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0, 1-mwait); glVertex2f(x,      y+(1-mwait)*size);
            glTexCoord2f(1, 1-mwait); glVertex2f(x+size, y+(1-mwait)*size);
            glTexCoord2f(0, 1);       glVertex2f(x,      y+(1-mwait)*size+mwait*size);
            glTexCoord2f(1, 1);       glVertex2f(x+size, y+(1-mwait)*size+mwait*size);
            glEnd();

            glPopMatrix();
            dontdraw:
            glPopMatrix();
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
