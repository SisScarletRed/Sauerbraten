#include "game.h"

extern int soundvol,
           getcolorbynum(int n);

namespace game
{
    vector<fpsent *> bestplayers;
    vector<const char *> bestteams;

    VARP(ragdoll, 0, 1, 1);
    VARP(ragdollmillis, 0, 10000, 300000);
    VARP(ragdollfade, 0, 1000, 300000);
    VARFP(playermodel, 0, 1, 4, changedplayermodel(); execute("playermodelchange");)
    VARP(forceplayermodels, 0, 0, 1);
    VARP(hidedead, 0, 0, 1);

    vector<fpsent *> ragdolls;

    void saveragdoll(fpsent *d)
    {
        if(!d->ragdoll || !ragdollmillis || (!ragdollfade && lastmillis > d->lastpain + ragdollmillis)) return;
        fpsent *r = new fpsent(*d);
        r->lastupdate = ragdollfade && lastmillis > d->lastpain + max(ragdollmillis - ragdollfade, 0) ? lastmillis - max(ragdollmillis - ragdollfade, 0) : d->lastpain;
        r->edit = NULL;
        r->ai = NULL;
        r->attackchan = r->idlechan = -1;
        if(d==player1) r->playermodel = playermodel;
        ragdolls.add(r);
        d->ragdoll = NULL;
    }

    void clearragdolls()
    {
        ragdolls.deletecontents();
    }

    void moveragdolls()
    {
        loopv(ragdolls)
        {
            fpsent *d = ragdolls[i];
            if(lastmillis > d->lastupdate + ragdollmillis)
            {
                delete ragdolls.remove(i--);
                continue;
            }
            moveragdoll(d);
        }
    }

    static const playermodelinfo playermodels[5] =
    {
        { "mrfixit", "mrfixit/blue", "mrfixit/red", "mrfixit/hudguns", NULL, "mrfixit/horns", { "mrfixit/armor/blue", "mrfixit/armor/green", "mrfixit/armor/yellow" }, "mrfixit", "mrfixit_blue", "mrfixit_red", true },
        { "snoutx10k", "snoutx10k/blue", "snoutx10k/red", "snoutx10k/hudguns", NULL, "snoutx10k/wings", { "snoutx10k/armor/blue", "snoutx10k/armor/green", "snoutx10k/armor/yellow" }, "snoutx10k", "snoutx10k_blue", "snoutx10k_red", true },
        //{ "ogro/green", "ogro/blue", "ogro/red", "mrfixit/hudguns", "ogro/vwep", NULL, { NULL, NULL, NULL }, "ogro", "ogro_blue", "ogro_red", false },
        { "ogro2", "ogro2/blue", "ogro2/red", "mrfixit/hudguns", NULL, "ogro2/quad", { "ogro2/armor/blue", "ogro2/armor/green", "ogro2/armor/yellow" }, "ogro", "ogro_blue", "ogro_red", true },
        { "inky", "inky/blue", "inky/red", "inky/hudguns", NULL, "inky/quad", { "inky/armor/blue", "inky/armor/green", "inky/armor/yellow" }, "inky", "inky_blue", "inky_red", true },
        { "captaincannon", "captaincannon/blue", "captaincannon/red", "captaincannon/hudguns", NULL, "captaincannon/quad", { "captaincannon/armor/blue", "captaincannon/armor/green", "captaincannon/armor/yellow" }, "captaincannon", "captaincannon_blue", "captaincannon_red", true }
    };

    int chooserandomplayermodel(int seed)
    {
        return (seed&0xFFFF)%(sizeof(playermodels)/sizeof(playermodels[0]));
    }

    const playermodelinfo *getplayermodelinfo(int n)
    {
        if(size_t(n) >= sizeof(playermodels)/sizeof(playermodels[0])) return NULL;
        return &playermodels[n];
    }

    const playermodelinfo &getplayermodelinfo(fpsent *d)
    {
        const playermodelinfo *mdl = getplayermodelinfo(d==player1 || forceplayermodels ? playermodel : d->playermodel);
        if(!mdl) mdl = getplayermodelinfo(playermodel);
        return *mdl;
    }

    void changedplayermodel()
    {
        if(player1->clientnum < 0) player1->playermodel = playermodel;
        if(player1->ragdoll) cleanragdoll(player1);
        loopv(ragdolls)
        {
            fpsent *d = ragdolls[i];
            if(!d->ragdoll) continue;
            if(!forceplayermodels)
            {
                const playermodelinfo *mdl = getplayermodelinfo(d->playermodel);
                if(mdl) continue;
            }
            cleanragdoll(d);
        }
        loopv(players)
        {
            fpsent *d = players[i];
            if(d == player1 || !d->ragdoll) continue;
            if(!forceplayermodels)
            {
                const playermodelinfo *mdl = getplayermodelinfo(d->playermodel);
                if(mdl) continue;
            }
            cleanragdoll(d);
        }
    }

    void preloadplayermodel()
    {
        loopi(sizeof(playermodels)/sizeof(playermodels[0]))
        {
            const playermodelinfo *mdl = getplayermodelinfo(i);
            if(!mdl) break;
            if(i != playermodel && (!multiplayer(false) || forceplayermodels)) continue;
            if(m_teammode)
            {
                preloadmodel(mdl->blueteam);
                preloadmodel(mdl->redteam);
            }
            else preloadmodel(mdl->ffa);
            if(mdl->vwep) preloadmodel(mdl->vwep);
            if(mdl->quad && !m_insta) preloadmodel(mdl->quad);
            if(!m_insta) loopj(3) if(mdl->armour[j]) preloadmodel(mdl->armour[j]);
        }
    }

    VAR(testquad, 0, 0, 1);
    VAR(testarmour, 0, 0, 1);
    VAR(testteam, 0, 0, 3);

    void renderplayer(fpsent *d, const playermodelinfo &mdl, int team, float fade, bool mainpass)
    {
        int lastaction = d->lastaction, hold = mdl.vwep || d->gunselect==GUN_PISTOL ? 0 : (ANIM_HOLD1+d->gunselect)|ANIM_LOOP, attack = ANIM_ATTACK1+d->gunselect, delay = mdl.vwep ? 300 : guns[d->gunselect].attackdelay+50;
        if(intermission && d->state!=CS_DEAD)
        {
            lastaction = 0;
            hold = attack = ANIM_LOSE|ANIM_LOOP;
            delay = 0;
            if(m_teammode ? bestteams.htfind(d->team)>=0 : bestplayers.find(d)>=0) hold = attack = ANIM_WIN|ANIM_LOOP;
        }
        else if(d->state==CS_ALIVE && d->lasttaunt && lastmillis-d->lasttaunt<1000 && lastmillis-d->lastaction>delay)
        {
            lastaction = d->lasttaunt;
            hold = attack = ANIM_TAUNT;
            delay = 1000;
        }
        modelattach a[5];
        static const char * const vweps[] = {"vwep/fist", "vwep/shotg", "vwep/chaing", "vwep/rocket", "vwep/rifle", "vwep/gl", "vwep/pistol"};
        int ai = 0;
        if((!mdl.vwep || d->gunselect!=GUN_FIST) && d->gunselect<=GUN_PISTOL)
        {
            int vanim = ANIM_VWEP_IDLE|ANIM_LOOP, vtime = 0;
            if(lastaction && d->lastattackgun==d->gunselect && lastmillis < lastaction + delay)
            {
                vanim = ANIM_VWEP_SHOOT;
                vtime = lastaction;
            }
            a[ai++] = modelattach("tag_weapon", mdl.vwep ? mdl.vwep : vweps[d->gunselect], vanim, vtime);
        }
        if(d->state==CS_ALIVE)
        {
            if((testquad || d->quadmillis) && mdl.quad)
                a[ai++] = modelattach("tag_powerup", mdl.quad, ANIM_POWERUP|ANIM_LOOP, 0);
            if(testarmour || d->armour)
            {
                int type = clamp(d->armourtype, (int)A_BLUE, (int)A_YELLOW);
                if(mdl.armour[type])
                    a[ai++] = modelattach("tag_shield", mdl.armour[type], ANIM_SHIELD|ANIM_LOOP, 0);
            }
        }
        if(mainpass)
        {
            d->muzzle = vec(-1, -1, -1);
            a[ai++] = modelattach("tag_muzzle", &d->muzzle);
        }
        const char *mdlname = mdl.ffa;
        switch(testteam ? testteam-1 : team)
        {
            case 1: mdlname = mdl.blueteam; break;
            case 2: mdlname = mdl.redteam; break;
        }
        renderclient(d, mdlname, a[0].tag ? a : NULL, hold, attack, delay, lastaction, intermission && d->state!=CS_DEAD ? 0 : d->lastpain, fade, ragdoll && mdl.ragdoll);
    }

    VARP(teamskins, 0, 0, 1);
    QFVARP(playernamesize, "size of the names above the player-models", 2.0f, 5.0f, 10.0f);
    QVARP(playernamezoffset, "height-offset of the names above the player-models", 0, 4, 10);
    QVARP(showteamhealth, "show text[1]/bar[2] with health/armor above the player-models (only playerteam, all teams in spec)", 0, 2, 2);
    QVARP(renderspectators, "render spectator-playermodels.. [1]: only if you're spec too, [2]: always", 0, 1, 2);

    extern int playerteamcolor,
               enemyteamcolor;

    void rendergame(bool mainpass)
    {
        if(mainpass) ai::render();

        if(intermission)
        {
            bestteams.shrink(0);
            bestplayers.shrink(0);
            if(m_teammode) getbestteams(bestteams);
            else getbestplayers(bestplayers);
        }

        startmodelbatches();

        fpsent *exclude = isthirdperson() ? NULL : followingplayer();
        loopv(players)
        {
            fpsent *d = players[i];
            bool renderspecs = (player1->state==CS_SPECTATOR && renderspectators==1) || renderspectators==2;
            if(d == player1 || !renderspecs || d->state==CS_SPAWNING || d->lifesequence < 0 || d == exclude || (d->state==CS_DEAD && hidedead)) continue;
            int team = 0;
            if(teamskins || m_teammode) team = isteam(player1->team, d->team) ? 1 : 2;
            renderplayer(d, getplayermodelinfo(d), team, (d->state==CS_SPECTATOR || d->state==CS_EDITING) ? 0.5f : 1.0f, mainpass);
            copystring(d->info, colorname(d));
            if(d->maxhealth>100) { defformatstring(sn)(" +%d", d->maxhealth-100); concatstring(d->info, sn); }
            if((isteam(player1->team, d->team) || player1->state == CS_SPECTATOR) && m_teammode && !m_insta) switch(showteamhealth)
            {
                case 1:
                {
                    string healthcolor, armourcolor;
                    if(d->health <= 25) strcpy(healthcolor, "\f3");
                    else if(d->health <= 50) strcpy(healthcolor, "\f6");
                    else strcpy(healthcolor, "\f0");
                    if(d->armour <= 25) strcpy(armourcolor, "\f3");
                    else if(d->armour <= 50) strcpy(armourcolor, "\f6");
                    else strcpy(armourcolor, "\f0");
                    defformatstring(thpa)("\n%s%d\f7|%s%d", healthcolor, d->health, armourcolor, d->armour); concatstring(d->info, thpa);
                    break;
                }
                case 2:
                {
                    int hmtype = PART_METER, hvalue = d->health, hcolor = 0x00FF00, hcolor2 = 0,
                        amtype = PART_METER, avalue = d->armour, acolor = 0xFF4500, acolor2 = 0;
                    if(d->health > 100)
                        hcolor = 0x006400; hcolor2 = 0x00FF00; hvalue = d->health - 100; hmtype = PART_METER_VS;
                    if((d->armourtype==A_YELLOW) && (d->armour > 100))
                        acolor = 0xFFD700; acolor2 = 0xFF4500; avalue = d->armour - 100; amtype = PART_METER_VS;
                    particle_health(d->abovehead().add(vec(0, 0, -2)), avalue/100.0f, amtype, 1, acolor, acolor2, 1.0f);
                    particle_health(d->abovehead().add(vec(0, 0, -1)), hvalue/100.0f, hmtype, 1, hcolor, hcolor2, 1.0f);
                    break;
                }
            }
            if (d->state!=CS_DEAD && showteamhealth && isteam(player1->team, d->team))
            	particle_text(d->abovehead().add(vec(0, 0, 1+showteamhealth+playernamezoffset)), d->info, PART_TEXT, 1, team ? (team==1 ? getcolorbynum(playerteamcolor) : getcolorbynum(enemyteamcolor)) : 0x1EC850, (player1->o.dist(d->o)) > 10*playernamesize ? playernamesize : (player1->o.dist(d->o))/10 );
            else if (d->state!=CS_DEAD)
            	particle_text(d->abovehead().add(vec(0, 0, playernamezoffset)), d->info, PART_TEXT, 1, team ? (team==1 ? getcolorbynum(playerteamcolor) : getcolorbynum(enemyteamcolor)) : 0x1EC850, (player1->o.dist(d->o)) > 10*playernamesize ? playernamesize : (player1->o.dist(d->o))/10 );
        }
        loopv(ragdolls)
        {
            fpsent *d = ragdolls[i];
            int team = 0;
            if(teamskins || m_teammode) team = isteam(player1->team, d->team) ? 1 : 2;
            float fade = 1.0f;
            if(ragdollmillis && ragdollfade)
                fade -= clamp(float(lastmillis - (d->lastupdate + max(ragdollmillis - ragdollfade, 0)))/min(ragdollmillis, ragdollfade), 0.0f, 1.0f);
            renderplayer(d, getplayermodelinfo(d), team, fade, mainpass);
        }
        if(isthirdperson() && !followingplayer() && (player1->state!=CS_DEAD || !hidedead)) renderplayer(player1, getplayermodelinfo(player1), teamskins || m_teammode ? 1 : 0, (player1->state==CS_SPECTATOR || player1->state==CS_EDITING) ? 0.5f : 1.0f, mainpass);
        rendermonsters();
        rendermovables();
        entities::renderentities();
        renderbouncers();
        renderprojectiles();
        if(cmode) cmode->rendergame();

        endmodelbatches();
    }

    VARP(hudgun, 0, 1, 1);
    VARP(hudgunsway, 0, 1, 1);
    VARP(teamhudguns, 0, 1, 1);
    VARP(chainsawhudgun, 0, 1, 1);
    VAR(testhudgun, 0, 0, 1);

    FVAR(swaystep, 1, 35.0f, 100);
    FVAR(swayside, 0, 0.04f, 1);
    FVAR(swayup, -1, 0.05f, 1);

    float swayfade = 0, swayspeed = 0, swaydist = 0;
    vec swaydir(0, 0, 0);

    void swayhudgun(int curtime)
    {
        fpsent *d = hudplayer();
        if(d->state != CS_SPECTATOR)
        {
            if(d->physstate >= PHYS_SLOPE)
            {
                swayspeed = min(sqrtf(d->vel.x*d->vel.x + d->vel.y*d->vel.y), d->maxspeed);
                swaydist += swayspeed*curtime/1000.0f;
                swaydist = fmod(swaydist, 2*swaystep);
                swayfade = 1;
            }
            else if(swayfade > 0)
            {
                swaydist += swayspeed*swayfade*curtime/1000.0f;
                swaydist = fmod(swaydist, 2*swaystep);
                swayfade -= 0.5f*(curtime*d->maxspeed)/(swaystep*1000.0f);
            }

            float k = pow(0.7f, curtime/10.0f);
            swaydir.mul(k);
            vec vel(d->vel);
            vel.add(d->falling);
            swaydir.add(vec(vel).mul((1-k)/(15*max(vel.magnitude(), d->maxspeed))));
        }
    }

    struct hudent : dynent
    {
        hudent() { type = ENT_CAMERA; }
    } guninterp;

    SVARP(hudgunsdir, "");
    QVARP(hudgunsalpha, "transparency of your hudguns", 0, 255, 255);

    void drawhudmodel(fpsent *d, int anim, float speed = 0, int base = 0)
    {
        if(d->gunselect>GUN_PISTOL) return;

        vec sway;
        vecfromyawpitch(d->yaw, 0, 0, 1, sway);
        float steps = swaydist/swaystep*M_PI;
        sway.mul(swayside*cosf(steps));
        sway.z = swayup*(fabs(sinf(steps)) - 1);
        sway.add(swaydir).add(d->o);
        if(!hudgunsway) sway = d->o;

#if 0
        if(player1->state!=CS_DEAD && player1->quadmillis)
        {
            float t = 0.5f + 0.5f*sinf(2*M_PI*lastmillis/1000.0f);
            color.y = color.y*(1-t) + t;
        }
#endif
        const playermodelinfo &mdl = getplayermodelinfo(d);
        defformatstring(gunname)("%s/%s", hudgunsdir[0] ? hudgunsdir : mdl.hudguns, guns[d->gunselect].file);
        if((m_teammode || teamskins) && teamhudguns)
            concatstring(gunname, d==player1 || isteam(d->team, player1->team) ? "/blue" : "/red");
        else if(testteam > 1)
            concatstring(gunname, testteam==2 ? "/blue" : "/red");
        modelattach a[2];
        d->muzzle = vec(-1, -1, -1);
        a[0] = modelattach("tag_muzzle", &d->muzzle);
        dynent *interp = NULL;
        if(d->gunselect==GUN_FIST && chainsawhudgun)
        {
            anim |= ANIM_LOOP;
            base = 0;
            interp = &guninterp;
        }
        rendermodel(NULL, gunname, anim, sway, testhudgun ? 0 : d->yaw+90, testhudgun ? 0 : d->pitch, MDL_LIGHT|MDL_HUD, interp, a, base, (int)ceil(speed), (float)hudgunsalpha/255.0f);
        if(d->muzzle.x >= 0) d->muzzle = calcavatarpos(d->muzzle, 12);
    }

    void drawhudgun()
    {
        fpsent *d = hudplayer();
        if(d->state==CS_SPECTATOR || d->state==CS_EDITING || !hudgun || editmode)
        {
            d->muzzle = player1->muzzle = vec(-1, -1, -1);
            return;
        }

        int rtime = guns[d->gunselect].attackdelay;
        if(d->lastaction && d->lastattackgun==d->gunselect && lastmillis-d->lastaction<rtime)
        {
            drawhudmodel(d, ANIM_GUN_SHOOT|ANIM_SETSPEED, rtime/17.0f, d->lastaction);
        }
        else
        {
            drawhudmodel(d, ANIM_GUN_IDLE|ANIM_LOOP);
        }
    }

    void renderavatar()
    {
        drawhudgun();
    }

    void renderplayerpreview(int model, int team, int weap)
    {
        static fpsent *previewent = NULL;
        if(!previewent)
        {
            previewent = new fpsent;
            previewent->o = vec(0, 0.9f*(previewent->eyeheight + previewent->aboveeye), previewent->eyeheight - (previewent->eyeheight + previewent->aboveeye)/2);
            previewent->light.color = vec(1, 1, 1);
            previewent->light.dir = vec(0, -1, 2).normalize();
            loopi(GUN_PISTOL-GUN_FIST) previewent->ammo[GUN_FIST+1+i] = 1;
        }
        previewent->gunselect = clamp(weap, int(GUN_FIST), int(GUN_PISTOL));
        previewent->yaw = fmod(lastmillis/10000.0f*360.0f, 360.0f);
        previewent->light.millis = -1;
        const playermodelinfo *mdlinfo = getplayermodelinfo(model);
        if(!mdlinfo) return;
        renderplayer(previewent, *mdlinfo, team >= 0 && team <= 2 ? team : 0, 1, false);
    }

    vec hudgunorigin(int gun, const vec &from, const vec &to, fpsent *d)
    {
        if(d->muzzle.x >= 0) return d->muzzle;
        vec offset(from);
        if(d!=hudplayer() || isthirdperson())
        {
            vec front, right;
            vecfromyawpitch(d->yaw, d->pitch, 1, 0, front);
            offset.add(front.mul(d->radius));
            if(d->type!=ENT_AI)
            {
                offset.z += (d->aboveeye + d->eyeheight)*0.75f - d->eyeheight;
                vecfromyawpitch(d->yaw, 0, 0, -1, right);
                offset.add(right.mul(0.5f*d->radius));
                offset.add(front);
            }
            return offset;
        }
        offset.add(vec(to).sub(from).normalize().mul(2));
        if(hudgun)
        {
            offset.sub(vec(camup).mul(1.0f));
            offset.add(vec(camright).mul(0.8f));
        }
        else offset.sub(vec(camup).mul(0.8f));
        return offset;
    }

    void preloadweapons()
    {
        const playermodelinfo &mdl = getplayermodelinfo(player1);
        loopi(NUMGUNS)
        {
            if(m_insta && (i != GUN_RIFLE && i != GUN_FIST)) continue;
            const char *file = guns[i].file;
            if(!file) continue;
            string fname;
            if((m_teammode || teamskins) && teamhudguns)
            {
                formatstring(fname)("%s/%s/blue", hudgunsdir[0] ? hudgunsdir : mdl.hudguns, file);
                preloadmodel(fname);
            }
            else
            {
                formatstring(fname)("%s/%s", hudgunsdir[0] ? hudgunsdir : mdl.hudguns, file);
                preloadmodel(fname);
            }
            formatstring(fname)("vwep/%s", file);
            preloadmodel(fname);
        }
    }

    bool soundsloaded = false;
    void preloadsounds()
    {
        for(int i = S_JUMP; i <= S_SPLASH2; i++) preloadsound(i);
        for(int i = S_JUMPPAD; i <= S_PISTOL; i++) preloadsound(i);
        for(int i = S_V_BOOST; i <= S_V_QUAD10; i++) preloadsound(i);
        for(int i = S_BURN; i <= S_HIT; i++) preloadsound(i);
        soundsloaded = true;
    }

    void preload()
    {
        soundsloaded = false;
        if(hudgun) preloadweapons();
        preloadbouncers();
        preloadplayermodel();
        if(soundvol) preloadsounds();
        entities::preloadentities();
        if(m_sp) preloadmonsters();
    }

}

