// creation of scoreboard
#include "game.h"

namespace game
{
    extern fpsent *flag1owner();
    extern fpsent *flag2owner();

    fpsent* getcurrentplayer()
    {
        if(player1->state==CS_SPECTATOR && followingplayer())
            return followingplayer();
        return player1;
    }

    int getgundamagetotal(int gun, fpsent* f = NULL)
    {
        fpsent* d = f ? f : getcurrentplayer();
        if(gun < 0)
        {
            int dmg = 0;
            loopi(MAXWEAPONS)
                dmg += d->detaileddamagetotal[i];
            return dmg;
        }
        else if(gun < MAXWEAPONS)
            return d->detaileddamagetotal[gun];
        return 0;
    }

    int getgundamagedealt(int gun, fpsent* f = NULL)
    {
        fpsent* d = f ? f : getcurrentplayer();
        if(gun < 0)
        {
            int dmg = 0;
            loopi(MAXWEAPONS)
                dmg += d->detaileddamagedealt[i];
            return dmg;
        }
        else if(gun < MAXWEAPONS)
            return d->detaileddamagedealt[gun];
        return 0;
    }

    int getgundamagereceived(int gun, fpsent* f = NULL)
    {
        fpsent* d = f ? f : getcurrentplayer();
        if(gun < 0)
        {
            int dmg = 0;
            loopi(MAXWEAPONS)
                dmg += d->detaileddamagereceived[i];
            return dmg;
        }
        else if(gun < MAXWEAPONS)
            return d->detaileddamagereceived[gun];
        return 0;
    }

    int getgundamagewasted(int gun, fpsent* f = NULL)
    {
        return getgundamagetotal(gun, f) - getgundamagedealt(gun, f);
    }

    int getgunnetdamage(int gun, fpsent* f = NULL)
    {
        return getgundamagedealt(gun, f) - getgundamagereceived(gun, f);
    }

    double getweaponaccuracy(int gun, fpsent *f = NULL)
    {
        double total = max(1.0, (double)getgundamagetotal(gun, f));
        return min(100.0, (getgundamagedealt(gun, f) / total) * 100.0);
    }

    VARP(scoreboard2d, 0, 1, 1);
    VARP(showservinfo, 0, 1, 1);
    VARP(showclientnum, 0, 0, 1);
    VARP(showpj, 0, 0, 1);
    VARP(showping, 0, 1, 1);
    VARP(showspectators, 0, 1, 1);
    VARP(highlightscore, 0, 1, 1);
    VARP(showconnecting, 0, 0, 1);

    HVARP(scoreboardtextcolorhead, 0, 0xFFFF00, 0xFFFFFF);
    HVARP(scoreboardtextcolor, 0, 0x00FFFF, 0xFFFFFF);
    HVARP(scoreboardhighlightcolor, 0, 0, 0xFFFFFF);
    HVARP(scoreboardbackgroundcolorplayerteam, 0, 0x0000AA, 0xFFFFFF);
    HVARP(scoreboardbackgroundcolorenemyteam, 0, 0xAA0000, 0xFFFFFF);
    VARP(showdemotime, 0, 1, 1);
    VARP(showfrags, 0, 0, 1);
    VARP(showflags, 0, 0, 1);
    VARP(shownetfrags, 0, 0, 1);
    VARP(netfragscolors, 0, 0, 1);
    VARP(showdamagedealt, 0, 0, 1);
    VARP(shownetdamage, 0, 0, 1);
    VARP(netdamagecolors, 0, 0, 1);
    VARP(showacc, 0, 0, 1);
    VARP(showserveracc, 0, 0, 1);
    VARP(showshortspecslist, 0, 0, 1);
    VARP(showdeaths, 0, 0, 1);
    VARP(showip, 0, 0, 1);
    HVARP(ipignorecolor, 0, 0xC4C420, 0xFFFFFF);
    VARP(showspectatorip, 0, 0, 1);

    #ifdef QUED32
    VARP(showcountries, 0, 3, 5);
    #endif // QUED32

    extern bool isduel(bool allowspec = false, int colors = 0);
    extern vector<fpsent *> players;

    hashset<teaminfo> teaminfos;

    void clearteaminfo()
    {
        teaminfos.clear();
    }

    void setteaminfo(const char *team, int frags)
    {
        teaminfo *t = teaminfos.access(team);
        if(!t) { t = &teaminfos[team]; copystring(t->team, team, sizeof(t->team)); }
        t->frags = frags;
    }

    teaminfo *getteaminfo(const char *team) { return teaminfos.access(team); }

    static inline bool playersort(const fpsent *a, const fpsent *b)
    {
        if(a->state==CS_SPECTATOR)
        {
            if(b->state==CS_SPECTATOR) return strcmp(a->name, b->name) < 0;
            else return false;
        }
        else if(b->state==CS_SPECTATOR) return true;
        if(m_ctf || m_collect)
        {
            if(a->flags > b->flags) return true;
            if(a->flags < b->flags) return false;
        }
        if(a->frags > b->frags) return true;
        if(a->frags < b->frags) return false;
        return strcmp(a->name, b->name) < 0;
    }

    void getbestplayers(vector<fpsent *> &best, bool fulllist)
    {
        loopv(players)
        {
            fpsent *o = players[i];
            if(o->state!=CS_SPECTATOR) best.add(o);
        }
        best.sort(playersort);
        if(!fulllist)
            while(best.length() > 1 && best.last()->frags < best[0]->frags)
                best.drop();
    }

    void getbestteams(vector<const char *> &best)
    {
        if(cmode && cmode->hidefrags())
        {
            vector<teamscore> teamscores;
            cmode->getteamscores(teamscores);
            teamscores.sort(teamscore::compare);
            while(teamscores.length() > 1 && teamscores.last().score < teamscores[0].score) teamscores.drop();
            loopv(teamscores) best.add(teamscores[i].team);
        }
        else
        {
            int bestfrags = INT_MIN;
            enumerates(teaminfos, teaminfo, t, bestfrags = max(bestfrags, t.frags));
            if(bestfrags <= 0) loopv(players)
            {
                fpsent *o = players[i];
                if(o->state!=CS_SPECTATOR && !teaminfos.access(o->team) && best.htfind(o->team) < 0) { bestfrags = 0; best.add(o->team); }
            }
            enumerates(teaminfos, teaminfo, t, if(t.frags >= bestfrags) best.add(t.team));
        }
    }

    static vector<scoregroup *> groups;
    static vector<fpsent *> spectators;

    vector<scoregroup *> getscoregroups() { return groups; }

    static inline bool scoregroupcmp(const scoregroup *x, const scoregroup *y)
    {
        if(!x->team)
        {
            if(y->team) return false;
        }
        else if(!y->team) return true;
        if(x->score > y->score) return true;
        if(x->score < y->score) return false;
        if(x->players.length() > y->players.length()) return true;
        if(x->players.length() < y->players.length()) return false;
        return x->team && y->team && strcmp(x->team, y->team) < 0;
    }

    int groupplayers()
    {
        int numgroups = 0;
        spectators.setsize(0);
        loopv(players)
        {
            fpsent *o = players[i];
            if(!showconnecting && !o->name[0]) continue;
            if(o->state==CS_SPECTATOR) { spectators.add(o); continue; }
            const char *team = m_teammode && o->team[0] ? o->team : NULL;
            bool found = false;
            loopj(numgroups)
            {
                scoregroup &g = *groups[j];
                if(team!=g.team && (!team || !g.team || strcmp(team, g.team))) continue;
                g.players.add(o);
                found = true;
            }
            if(found) continue;
            if(numgroups>=groups.length()) groups.add(new scoregroup);
            scoregroup &g = *groups[numgroups++];
            g.team = team;
            if(!team) g.score = 0;
            else if(cmode && cmode->hidefrags()) g.score = cmode->getteamscore(o->team);
            else { teaminfo *ti = teaminfos.access(team); g.score = ti ? ti->frags : 0; }
            g.players.setsize(0);
            g.players.add(o);
        }
        loopi(numgroups) groups[i]->players.sort(playersort);
        spectators.sort(playersort);
        groups.sort(scoregroupcmp, 0, numgroups);
        return numgroups;
    }

    const char *ownteamstatus()
    {
        int numgroups = groupplayers();
        if(!m_teammode || numgroups < 2) return NULL;

        scoregroup *ownteam = NULL;
        fpsent *f = followingplayer();
        if(!f && player1->state==CS_SPECTATOR) return NULL;
        if(!f && player1->state!=CS_SPECTATOR) f = player1;

        loopv(groups) if(groups[i] && isteam(f->team, groups[i]->team)) ownteam = groups[i];
        loopv(groups) if(groups[i] && ownteam != groups[i])
        {
            if(groups[i]->score > ownteam->score) return "\f3Losing";
            else if(groups[i]->score < ownteam->score) return "\f0Winning";
        }
        return "\f2Draw";
    }

    bool isclanwar(bool allowspec, int colors)
    {
        extern int mastermode;
        int numgroups = groupplayers();
        if(!m_teammode || (!allowspec && player1->state==CS_SPECTATOR) || numgroups < 2 || mastermode < MM_LOCKED) return false;

        groupplayers();
        fpsent *f = followingplayer();
        if(!f && player1->state != CS_SPECTATOR) f = player1;

        scoregroup *g1 = groups[0], *g2 = groups[1];
        if(f && isteam(f->team, groups[1]->team))
        {
            g1 = groups[1];
            g2 = groups[0];
        }

        char *g1clan = g1->isclan();
        char *g2clan = g2->isclan();
        if(g1clan || g2clan)
        {
            if(!g1clan) g1clan = newstring("mixed");
            if(!g2clan) g2clan = newstring("mixed");

			if(!colors)	formatstring(battleheadline)("%s(%d) vs %s(%d)", g1clan, g1->score, g2clan, g2->score);
			else if(colors==1 || !f)
                formatstring(battleheadline)("\f2%s\f7(%d) vs \f2%s\f7(%d)", g1clan, g1->score, g2clan, g2->score);
			else
            {
				bool winning = g1->score > g2->score;
				bool draw = g1->score == g2->score;
				formatstring(battleheadline)("%s%s(\fs%s%d\fr) \fs\f7vs\fr %s(\fs%s%d\fr)", winning ? "\f0" : draw ? "\f2" : "\f3", g1clan, winning ? "\f0" : draw ? "\f2" : "\f3", g1->score, g2clan, winning ? "\f0" : draw ? "\f2" : "\f3", g2->score);
			}
			return groups[0]->players.length() == groups[1]->players.length();
        }
        return false;
    }

    extern const char *cutclantag(const char *name);
    const char *colorclantag(const char *name)
    {
        strtool buf;
        loopv(clantags) if(strstr(name, clantags[i]->name))
        {
            buf.append("\fs\f9");
            buf.append((const char *)clantags[i]->name);
            buf.append("\fr");
            buf.append(cutclantag(name));
        }
        if(!buf.str()[0]) buf.append(name);
        return buf.str();
    }
    ICOMMAND(colorclantag, "s", (const char *name), result(colorclantag(name)));

    void formatdmg(char* buff, int bufflen, int d)
    {
        if(abs(d) < 1000)
            snprintf(buff, bufflen, "%d", d);
        else
            snprintf(buff, bufflen, "%d.%dk", d/1000, abs((d%1000)/100));
    }

    void getcolor(int val, int &color)
    {
        if(val>=0)
            color = 0x00FF00;
        else
            color = 0xFF0000;
    }

    void fragwrapper(g3d_gui &g, int frags, int deaths)
    {
        g.pushlist();
        g.textf("%d", scoreboardtextcolor, NULL, NULL, frags);
        int net = frags-deaths, c = scoreboardtextcolor;
        if(netfragscolors) getcolor(net, c);
        g.textf(":", 0x888888, NULL);
        g.textf("%d", c, NULL, NULL, net);
        g.poplist();
    }

    void dmgwrapper(g3d_gui &g, int dealt, int net)
    {
        char buff[10];
        g.pushlist();
        formatdmg(buff, 10, dealt);
        g.textf("%s", scoreboardtextcolor, NULL, NULL, buff);
        if(shownetdamage)
        {
            int c = scoreboardtextcolor;
            if(netdamagecolors) getcolor(net, c);
            g.textf(":", 0x888888, NULL);
            formatdmg(buff, 10, net);
            g.textf("%s", c, NULL, NULL, buff);
        }
        g.poplist();
    }

    template<typename T>
    static inline bool displayextinfo(T cond = T(1))
    {
        return cond && (isconnected(false) || demoplayback);
    }

    static inline void renderip(g3d_gui &g, fpsent *o)
    {
        if(o->extdatawasinit) g.textf("%s", scoreboardtextcolor, NULL, NULL, GeoIP_num_to_addr(endianswap(o->extdata.data.ip)));
        else g.text("??", scoreboardtextcolor);
    }

    void renderscoreboard(g3d_gui &g, bool firstpass)
    {
        const ENetAddress *address = connectedpeer();
        if(showservinfo && address)
        {
            string hostname;
            if(enet_address_get_host_ip(address, hostname, sizeof(hostname)) >= 0)
            {
                if(servinfo[0]) g.titlef("%.25s", scoreboardtextcolorhead, NULL, NULL, servinfo);
                else g.titlef("%s:%d", scoreboardtextcolorhead, NULL, NULL, hostname, address->port);
            }
        }

        g.pushlist();
        g.spring();
        g.text(server::prettymodename(gamemode), scoreboardtextcolorhead);
        g.separator();
        const char *mname = getclientmap();
        g.text(mname[0] ? mname : "[new map]", scoreboardtextcolorhead);
        extern int gamespeed;
        if(gamespeed != 100) { g.separator(); g.textf("%d.%02dx", scoreboardtextcolorhead, NULL, NULL, gamespeed/100, gamespeed%100); }
        if(m_timed && mname[0] && (maplimit >= 0 || intermission))
        {
            g.separator();
            if(intermission) g.text("intermission", scoreboardtextcolorhead);
            else
            {
                int secs = max(maplimit-lastmillis, 0)/1000, mins = secs/60;
                secs %= 60;
                g.pushlist();
                g.strut(mins >= 10 ? 4.5f : 3.5f);
                g.textf("%d:%02d", scoreboardtextcolorhead, NULL, NULL, mins, secs);
                g.poplist();
            }
        }
        if(ispaused()) { g.separator(); g.text("paused", scoreboardtextcolorhead); }
        g.spring();
        g.poplist();

        g.separator();

        int numgroups = groupplayers();
        loopk(numgroups)
        {
            if((k%2)==0) g.pushlist(); // horizontal

            scoregroup &sg = *groups[k];
            int bgcolor = sg.team && m_teammode ? (isteam(player1->team, sg.team) ? scoreboardbackgroundcolorplayerteam : scoreboardbackgroundcolorenemyteam) : 0,
                fgcolor = scoreboardtextcolorhead;

            g.pushlist(); // vertical
            g.pushlist(); // horizontal

            #define loopscoregroup(o, b) \
                loopv(sg.players) \
                { \
                    fpsent *o = sg.players[i]; \
                    b; \
                }

            g.pushlist();
            if(sg.team && m_teammode)
            {
                g.pushlist();
                g.background(bgcolor, numgroups>1 ? 3 : 5);
                g.strut(1);
                g.poplist();
            }
            g.text("", 0, " ");
            loopscoregroup(o,
            {
                bool isignored = ipignore::isignored(o->clientnum, NULL);
                if((o==player1 && highlightscore && (multiplayer(false) || demoplayback || players.length() > 1)) || isignored)
                {
                    g.pushlist();
                    g.background(isignored ? ipignorecolor : scoreboardhighlightcolor, numgroups>1 ? 3 : 5);
                }
                const playermodelinfo &mdl = getplayermodelinfo(o);
                const char *icon = sg.team && m_teammode ? (isteam(player1->team, sg.team) ? mdl.blueicon : mdl.redicon) : mdl.ffaicon;
                g.text("", 0, icon);

                bool oisfriend = false;
                loopv(friends) if(friends[i]->name == o->name) oisfriend = true;
                if(((o==player1 || oisfriend) && highlightscore && (multiplayer(false) || demoplayback || players.length()>1)) || isignored) g.poplist();
            });
            g.poplist();

            if(sg.team && m_teammode)
            {
                g.pushlist(); // vertical

                char *clan = sg.isclan();
                if(sg.score>=10000) g.textf("%s: WIN", fgcolor, NULL, NULL, sg.team);
                else if(clan) g.textf("%s\fs\f4(%s)\fr: %d", fgcolor, NULL, NULL, clan, sg.team, sg.score);
                else g.textf("%s: %d", fgcolor, NULL, NULL, sg.team, sg.score);

                g.pushlist(); // horizontal
            }

            if((m_ctf || m_collect) && showflags)
            {
               g.pushlist();
               g.strut(m_ctf ? 5 : 6);
               g.text(m_ctf ? "flags" : "skulls", fgcolor);
               loopscoregroup(o, g.textf("%d", scoreboardtextcolor, NULL, NULL, o->flags));
               g.poplist();
            }

            if(!cmode || !cmode->hidefrags() || showfrags)
            {
                g.pushlist();
                g.strut(shownetfrags ? 7 : 5);
                g.text("frags", fgcolor);
                if(shownetfrags) { loopscoregroup(o, fragwrapper(g, o->frags, o->deaths)); }
                else loopscoregroup(o, g.textf("%d", scoreboardtextcolor, NULL, NULL, o->frags));
                g.poplist();
            }

            if(showdeaths)
            {
                g.pushlist();
                g.strut(6);
                g.text("deaths", fgcolor);
                loopscoregroup(o, g.textf("%d", scoreboardtextcolor, NULL, NULL, o->deaths));
                g.poplist();
            }

            g.pushlist();
            g.text("name", fgcolor);
            g.strut(15);
            loopscoregroup(o,
            {
                int status = o->state!=CS_DEAD ? scoreboardtextcolor : 0x606060;
                if(o->privilege)
                {
                    status = hud::guiprivcolor(o->privilege>=PRIV_ADMIN);
                    if(o->state==CS_DEAD) status = (status>>1)&0x7F7F7F;
                }
                g.textf("%s ", status, NULL, NULL, colorname(o));
            });
            g.poplist();

            if(showdamagedealt)
            {
                g.pushlist();
                g.strut(shownetdamage ? 10 : 6);
                g.text("dmg", fgcolor);
                loopscoregroup(o, dmgwrapper(g, getgundamagedealt(-1,o), getgunnetdamage(-1,o)));
                g.poplist();
            }

            if(showacc)
            {
                g.pushlist();
                g.strut(6);
                g.text("acc", fgcolor);
                if(showserveracc && connectedpeer()) { loopscoregroup(o, g.textf("%d", scoreboardtextcolor, NULL, NULL, (o ? o->extdata.data.acc : 0))); }
                else loopscoregroup(o, g.textf("%.2lf", scoreboardtextcolor, NULL, NULL, getweaponaccuracy(-1, o)));
                g.poplist();
            }

            if(showip)
            {
                g.pushlist();
                g.strut(11);
                g.text("ip", fgcolor);
                loopscoregroup(o, renderip(g, o));
                g.poplist();
            }

            #ifdef QUED32
            if(showcountries)
            {
                g.pushlist();
                g.text("country", fgcolor);
                g.strut(7);
                loopscoregroup(o,
                {
                    const char icon[MAXSTRLEN] = "";
                    const char *countrycode = GeoIP_country_code_by_ipnum(geoip, endianswap(o->extdata.data.ip));
                    const char *country = (showcountries&2) ? countrycode : (showcountries&4) ? GeoIP_country_name_by_ipnum(geoip, endianswap(o->extdata.data.ip)) : "";
                    if(showcountries&1) formatstring(icon)("%s.png", countrycode);
                    g.textf("%s", 0xFFFFFF, (showcountries&1) ? icon : NULL, NULL, country);
                });
                g.poplist();
            }
            #endif // QUED32

            if(multiplayer(false) || demoplayback)
            {
                if(showpj)
                {
                    g.pushlist();
                    g.strut(6);
                    g.text("pj", fgcolor);
                    loopscoregroup(o,
                    {
                        if(o->state==CS_LAGGED) g.text("LAG", scoreboardtextcolor);
                        else g.textf("%d", scoreboardtextcolor, NULL, NULL, o->plag);
                    });
                    g.poplist();
                }

                if(showping)
                {
                    g.pushlist();
                    g.text("ping", fgcolor);
                    g.strut(6);
                    loopscoregroup(o,
                    {
                        fpsent *p = o->ownernum >= 0 ? getclient(o->ownernum) : o;
                        if(!p) p = o;
                        if(!showpj && p->state==CS_LAGGED) g.text("LAG", scoreboardtextcolor);
                        else g.textf("%.1f", scoreboardtextcolor, NULL, NULL, p->ping);
                    });
                    g.poplist();
                }
            }

            if(showclientnum || player1->privilege>=PRIV_MASTER)
            {
                g.pushlist();
                g.text("cn", fgcolor);
                g.strut(3);
                loopscoregroup(o, g.textf("%d", scoreboardtextcolor, NULL, NULL, o->clientnum));
                g.poplist();
            }

            if(sg.team && m_teammode)
            {
                g.poplist(); // horizontal
                g.poplist(); // vertical
            }

            g.poplist(); // horizontal
            g.poplist(); // vertical

            if(k+1<numgroups && (k+1)%2) g.space(2);
            else g.poplist(); // horizontal
        }


        if(showspectators && spectators.length())
        {
            if(!showshortspecslist && (showclientnum || showping || player1->privilege>=PRIV_MASTER))
            {
                g.pushlist();
                g.pushlist();
                g.text("spectator", scoreboardtextcolorhead, " ");
                loopv(spectators)
                {
                    fpsent *o = spectators[i];
                    int status = scoreboardtextcolor;
                    bool isignored = ipignore::isignored(o->clientnum, NULL);
                    if(o->privilege) status = hud::guiprivcolor(o->privilege>=PRIV_ADMIN);
                    if((o==player1 && highlightscore) || isignored)
                    {
                        g.pushlist();
                        g.background(isignored ? ipignorecolor : scoreboardhighlightcolor, 3);
                    }
                    g.text(colorname(o), status, "spectator");
                    if((o==player1 && highlightscore) || isignored) g.poplist();
                }
                g.poplist();

                if(showclientnum)
                {
                    g.space(1);
                    g.pushlist();
                    g.text("cn", scoreboardtextcolorhead);
                    loopv(spectators) g.textf("%d", scoreboardtextcolor, NULL, NULL, spectators[i]->clientnum);
                    g.poplist();
                }

                if(displayextinfo(showspectatorip))
                {
                    g.space(1);
                    g.pushlist();
                    g.text("ip", scoreboardtextcolorhead);
                    loopv(spectators) renderip(g, spectators[i]);
                    g.poplist();
                }

                #ifdef QUED32
                if(showcountries)
                {
                    g.pushlist();
                    g.text("country", 0xFFFF80);
                    g.strut(7);
                    loopv(spectators)
                    {
                        const char icon[MAXSTRLEN] = "";
                        const char *countrycode = GeoIP_country_code_by_ipnum(geoip, endianswap(spectators[i]->extdata.data.ip));
                        const char *country = (showcountries&2) ? countrycode : (showcountries&4) ? GeoIP_country_name_by_ipnum(geoip, endianswap(spectators[i]->extdata.data.ip)) : "";
                        if(showcountries&1) formatstring(icon)("%s.png", countrycode);
                        g.textf("%s", 0xFFFFFF, (showcountries&1) ? icon : NULL, NULL, country);
                    };
                    g.poplist();
                }
                #endif // QUED32

                if(showping && (multiplayer(false) || demoplayback))
                {
                    g.space(1);
                    g.pushlist();
                    g.text("ping", scoreboardtextcolorhead);
                    loopv(spectators)
                    {
                        fpsent *p = spectators[i]->ownernum >= 0 ? getclient(spectators[i]->ownernum) : spectators[i];
                        if(!p) p = spectators[i];
                        if(p->state==CS_LAGGED) g.text("LAG", scoreboardtextcolor);
                        else g.textf("%d", scoreboardtextcolor, NULL, NULL, (int)p->ping);
                    }
                    g.poplist();
                }
                g.poplist();
            }
            else
            {
                g.textf("%d spectator%s", scoreboardtextcolorhead, " ", NULL, spectators.length(), spectators.length()!=1 ? "s" : "");
                loopv(spectators)
                {
                    if((i%3)==0)
                    {
                        g.pushlist();
                        g.text("", scoreboardtextcolor, "spectator");
                    }
                    fpsent *o = spectators[i];
                    int status = scoreboardtextcolor;
                    bool isignored = ipignore::isignored(o->clientnum, NULL);
                    if(o->privilege) status = hud::guiprivcolor(o->privilege);
                    if((o==player1 && highlightscore) || isignored)
                    {
                        g.pushlist();
                        g.background(isignored ? ipignorecolor : scoreboardhighlightcolor);
                    }
                    g.text(colorname(o), status);
                    if((o==player1 && highlightscore) || isignored) g.poplist();
                    if(i+1<spectators.length() && (i+1)%3) g.space(1);
                    else g.poplist();
                }
            }
        }
    }

    struct scoreboardgui : g3d_callback
    {
        bool showing;
        vec menupos;
        int menustart;

        scoreboardgui() : showing(false) {}

        void show(bool on)
        {
            if(!showing && on)
            {
                menupos = menuinfrontofplayer();
                menustart = starttime();
            }
            showing = on;
        }

        void gui(g3d_gui &g, bool firstpass)
        {
            g.start(menustart, 0.03f, NULL, false);
            renderscoreboard(g, firstpass);
            g.end();
        }

        void render()
        {
            if(showing) g3d_addgui(this, menupos, (scoreboard2d ? GUI_FORCE_2D : GUI_2D | GUI_FOLLOW) | GUI_BOTTOM);
        }

    } scoreboard;

    static void rendericon(g3d_gui &g, int i)
    {
        int icons[MAXWEAPONS] = {0, GUN_SG, GUN_CG, GUN_RL, GUN_RIFLE, GUN_GL, GUN_PISTOL};
        g.textwithtextureicon(NULL, 0, "packages/hud/items.png", false, true,
                              0.25f*((HICON_FIST+icons[i])%4),
                              0.25f*((HICON_FIST+icons[i])/4),
                              0.25f, 0.25f);
    }

    void renderplayerstats(g3d_gui &g, bool firstpass)
    {
        g.titlef("Stats for %s(%d)", scoreboardtextcolorhead, NULL, NULL,
                 getcurrentplayer()->name, getcurrentplayer()->clientnum);
        g.separator();

        g.pushlist();

        g.pushlist();
        g.space(1);
        g.strut(4);
        loopi(MAXWEAPONS)
            rendericon(g, i);
        g.space(1);
        g.poplist();

        g.space(2);

        g.pushlist();
        g.text("Acc", scoreboardtextcolorhead);
        g.strut(6);
        loopi(MAXWEAPONS)
            g.textf("%.2lf", scoreboardtextcolor, NULL, NULL, getweaponaccuracy(i));
        g.space(1);
        g.textf("%.2lf", 0x00C8FF, NULL, NULL, getweaponaccuracy(-1));
        g.poplist();

        g.space(2);

        g.pushlist();
        g.text("Dmg", scoreboardtextcolorhead);
        g.strut(6);
        loopi(MAXWEAPONS)
            g.textf("%d", scoreboardtextcolor, NULL, NULL, getgundamagedealt(i));
        g.space(1);
        g.textf("%d", scoreboardtextcolor, NULL, NULL, getgundamagedealt(-1));
        g.poplist();

        g.space(2);

        g.pushlist();
        g.text("Taken", scoreboardtextcolorhead);
        g.strut(6);
        loopi(MAXWEAPONS)
            g.textf("%d", scoreboardtextcolor, NULL, NULL, getgundamagereceived(i));
        g.space(1);
        g.textf("%d", scoreboardtextcolor, NULL, NULL, getgundamagereceived(-1));
        g.poplist();

        g.space(2);

        g.pushlist();
        g.text("Net", scoreboardtextcolorhead);
        g.strut(6);
        int net = 0, color = 0;
        loopi(MAXWEAPONS)
        {
            net = getgunnetdamage(i);
            g.textf("%d", scoreboardtextcolor, NULL, NULL, net);
        }
        net = getgunnetdamage(-1);
        getcolor(net, color);
        g.space(1);
        g.textf("%d", color, NULL, NULL, net);
        g.poplist();

        g.poplist();
    }

    struct playerstatsgui : g3d_callback
    {
        bool showing;
        vec menupos;
        int menustart;

        playerstatsgui() : showing(false) {}

        void show(bool on)
        {
            if(!showing && on)
            {
                menupos = menuinfrontofplayer();
                menustart = starttime();
            }
            showing = on;
        }

        void gui(g3d_gui &g, bool firstpass)
        {
            g.start(menustart, 0.03f, NULL, false);
            renderplayerstats(g, firstpass);
            g.end();
        }

        void render()
        {
            if(showing)
                g3d_addgui(this, menupos, (scoreboard2d ? GUI_FORCE_2D : GUI_2D | GUI_FOLLOW) | GUI_BOTTOM);
        }

    } playerstats;

    void g3d_gamemenus()
    {
        playerstats.render();
        scoreboard.render();
    }

    // scoreboard
    VARFN(scoreboard, showscoreboard, 0, 0, 1, scoreboard.show(showscoreboard!=0));
    void showscores(bool on)
    {
        showscoreboard = on ? 1 : 0;
        scoreboard.show(on);
    }
    ICOMMAND(showscores, "D", (int *down), showscores(*down!=0));

    // player stats
    void showplayerstats(bool on)
    {
        playerstats.show(on);
    }
    ICOMMAND(showplayerstats, "D", (int *down), showplayerstats(*down!=0));
}

