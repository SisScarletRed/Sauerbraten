#include "masterboard.h"
#include "geoip.h"

namespace game
{
    extern int gamespeed;
    extern int mastermode;
    extern void setmaster(const char *arg, const char *who);
    extern void setmastermode(int *val);
    extern void kick(const char *victim, const char *reason);
    extern void togglespectator(int val, const char *who);
    extern void setteam(const char *arg1, const char *arg2);
}

using namespace game;
namespace master
{
    VARP(mastservinfo, 0, 1, 1);
    VARP(boardextinfosync, 0, 1, 1);

    VARP(mastclientnum, 0, 1, 1);
    VARP(mastping, 0, 1, 1);
    VARP(mastspectators, 0, 1, 1);
    VARP(mastip, 0, 1, 1);

    VARP(mastkick, 0, 1, 1);
    VARP(mastspec, 0, 1, 1);
    VARP(mastteams, 0, 1, 1);
    VARP(mastmaster, 0, 1, 1);

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

    void getbestplayers(vector<fpsent *> &best)
    {
        loopv(players)
        {
            fpsent *o = players[i];
            if(o->state!=CS_SPECTATOR) best.add(o);
        }
        best.sort(playersort);
        while(best.length() > 1 && best.last()->frags < best[0]->frags) best.drop();
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

    struct scoregroup : teamscore
    {
        vector<fpsent *> players;
    };
    static vector<scoregroup *> groups;
    static vector<fpsent *> spectators;

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

    static int groupplayers()
    {
        int numgroups = 0;
        spectators.setsize(0);
        loopv(players)
        {
            fpsent *o = players[i];
            if(!o->name[0]) continue;
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

    void rendermasterboard(g3d_gui *g)
    {
        g->allowautotab(false);

        const ENetAddress *address = connectedpeer();
        if(mastservinfo && address)
        {
            string hostname;
            if(enet_address_get_host_ip(address, hostname, sizeof(hostname)) >= 0)
            {
                if(servinfo[0]) g->titlef("%.25s", 0xFFFFFF, NULL, NULL, servinfo);
                else g->titlef("%s:%d", 0xFFFFFF, NULL, NULL, hostname, address->port);
            }
        }

        g->pushlist();
        g->spring();
        g->text(server::prettymodename(gamemode), 0xFFFFFF);
        g->separator();
        const char *mname = getclientmap();
        g->text(mname[0] ? mname : "[new map]", 0xFFFFFF);
        if(game::gamespeed != 100) { g->separator(); g->textf("%d.%02dx", 0xFFFFFF, NULL, NULL, game::gamespeed/100, game::gamespeed%100); }
        if(m_timed && mname[0] && (maplimit >= 0 || intermission))
        {
            g->separator();
            if(intermission) g->text("intermission", 0xFFFFFF);
            else
            {
                int secs = max(maplimit-lastmillis, 0)/1000, mins = secs/60;
                secs %= 60;
                g->pushlist();
                g->strut(mins >= 10 ? 4.5f : 3.5f);
                g->textf("%d:%02d", 0xFFFFFF, NULL, NULL, mins, secs);
                g->poplist();
            }
        }
        if(ispaused()) { g->separator(); g->text("paused", 0xFFFFFF); }
        g->spring();
        g->poplist();

        g->separator();

        if(player1->privilege >= PRIV_MASTER)
        {
            if(g->button("Relinquish Master", 0xBB0000, "action")&G3D_UP)
                setmaster("0", player1->name);
        }
        else
        {
            if(g->button("Claim Master", 0x00BB00, "action")&G3D_UP)
                setmaster("1", player1->name);
        }

        g->pushlist();
        g->text("Mastermode: ", 0xFFFFFF);
        if(g->button(tempformatstring("%sOpen (0)", game::mastermode==0 ? "[set] " : ""), game::mastermode==0 ? 0xFFFFFF : 0xAAAAAA, NULL)&G3D_UP)
            addmsg(N_MASTERMODE, "ri", 0);
        if(g->button(tempformatstring("%sVeto (0)", game::mastermode==1 ? "[set] " : ""), game::mastermode==1 ? 0xFFFFFF : 0xAAAAAA, NULL)&G3D_UP)
            addmsg(N_MASTERMODE, "ri", 1);
        if(g->button(tempformatstring("%sLocked (0)", game::mastermode==2 ? "[set] " : ""), game::mastermode==2 ? 0xFFFFFF : 0xAAAAAA, NULL)&G3D_UP)
            addmsg(N_MASTERMODE, "ri", 2);
        if(g->button(tempformatstring("%sPrivate (0)", game::mastermode==3 ? "[set] " : ""), game::mastermode==3 ? 0xFFFFFF : 0xAAAAAA, NULL)&G3D_UP)
            addmsg(N_MASTERMODE, "ri", 3);
        g->poplist();

        g->separator();

        int numgroups = groupplayers();
        loopk(numgroups)
        {
            if((k%2)==0) g->pushlist(); // horizontal

            scoregroup &sg = *groups[k];
            int teamcolor = sg.team && m_teammode ? (isteam(player1->team, sg.team) ? 0x0000FF : 0xFF0000) : 0xFFFFFF;

            g->pushlist(); // vertical
            g->pushlist(); // horizontal

            #define loopscoregroup(o, b) \
                loopv(sg.players) \
                { \
                    fpsent *o = sg.players[i]; \
                    b; \
                }

            g->pushlist();
            if(sg.team && m_teammode)
            {
                g->pushlist();
                g->strut(1);
                g->poplist();
            }
            g->pushlist();
            g->strut(1);
            g->poplist();
            loopscoregroup(o,
            {
                if(o==player1 && (multiplayer(false) || demoplayback || players.length() > 1))
                {
                    g->pushlist();
                    g->background(0x808080, numgroups>1 ? 3 : 5);
                }
                g->text("", 0);
                if(o==player1 && (multiplayer(false) || demoplayback || players.length() > 1)) g->poplist();
            });
            g->poplist();

            if(sg.team && m_teammode)
            {
                g->pushlist(); // vertical

                if(sg.score>=10000) g->textf("%s: WIN", teamcolor, NULL, NULL, sg.team);
                else g->textf("%s: %d", teamcolor, NULL, NULL, sg.team, sg.score);

                g->pushlist(); // horizontal
            }

            if(mastclientnum)
            {
                g->pushlist();
                g->strut(4);
                g->text("cn", 0xA0A0A0);
                loopscoregroup(o, g->textf("%d", 0xFFFFFF, NULL, NULL, o->clientnum));
                g->poplist();
            }

            g->pushlist();
            g->text("name", 0xA0A0A0);
            g->strut(13);
            loopscoregroup(o,
            {
                int status = o->state!=CS_DEAD ? 0xFFFFFF : 0x606060;
                if(o->privilege)
                {
                    status = o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x00FF00;
                    if(o->state==CS_DEAD) status = (status>>1)&0x7F7F7F;
                }
                g->textf("%s ", status, NULL, NULL, colorname(o));
            });
            g->poplist();

            if(mastip)
            {
                g->pushlist();
                g->strut(7);
                g->text("ip", 0xA0A0A0);
                loopscoregroup(o, g->textf("%s", 0xFFFFFF, NULL, NULL, GeoIP_num_to_addr(endianswap(o->extdata.data.ip))));
                g->poplist();
            }

            if(mastkick)
            {
                g->pushlist();
                g->text("kick", 0xA0A0A0);
                g->strut(5);
                loopscoregroup(o,
                {
                    if(g->button("kick", 0xFF0000, NULL)&G3D_UP)
                        kick(o->name, "");
                });
                g->poplist();
            }

            if(mastspec)
            {
                g->pushlist();
                g->text("spec", 0xA0A0A0);
                g->strut(6);
                loopscoregroup(o,
                {
                    if(g->button(tempformatstring("%s", o->state==CS_SPECTATOR ? "unspec" : "spec"), 0xFF0000, NULL)&G3D_UP)
                        togglespectator(o->state==CS_SPECTATOR ? 0 : 1, o->name);
                });
                g->poplist();
            }

            if(mastteams)
            {
                g->pushlist();
                g->text("teams", 0xA0A0A0);
                g->strut(6);
                loopscoregroup(o,
                {
                    if(g->button(tempformatstring("\f%d%s", !strcmp(o->team, "good") ? 0 : 3, o->team), 0xFFFFFF, NULL)&G3D_UP)
                        setteam(o->name, !strcmp(o->team, "good") ? "evil" : "good");
                });
                g->poplist();
            }

            if(mastmaster)
            {
                g->pushlist();
                g->text("master", 0xA0A0A0);
                g->strut(7);
                loopscoregroup(o,
                {
                    if(g->button(tempformatstring("%s", o->privilege>=PRIV_MASTER ? "take" : "give"), o->privilege>=PRIV_MASTER ? 0xFF0000 : 0x00FF00, NULL)&G3D_UP)
                        setmaster(o->privilege>=PRIV_MASTER ? "0" : "1", o->name);
                });
                g->poplist();
            }

            if(multiplayer(false) || demoplayback)
            {
                if(mastping)
                {
                    g->pushlist();
                    g->text("ping", 0xA0A0A0);
                    g->strut(4);
                    loopscoregroup(o,
                    {
                        fpsent *p = o->ownernum >= 0 ? getclient(o->ownernum) : o;
                        if(!p) p = o;
                        if(p->state==CS_LAGGED) g->text("LAG", 0xFFFFFF);
                        else g->textf("%.1f", 0xFFFFFF, NULL, NULL, p->ping);
                    });
                    g->poplist();
                }
            }

            if(sg.team && m_teammode)
            {
                g->poplist(); // horizontal
                g->poplist(); // vertical
            }

            g->poplist(); // horizontal
            g->poplist(); // vertical

            if(k+1<numgroups && (k+1)%2) g->space(2);
            else g->poplist(); // horizontal
        }

        if(mastspectators && spectators.length())
        {
            g->pushlist();
            g->text("specs:", 0xA0A0A0, " ");

            g->pushlist();
            g->text("name", 0xA0A0A0);
            g->strut(13);
            loopv(spectators)
            {
                fpsent *o = spectators[i];
                int status = 0xFFFFFF;
                if(o->privilege) status = o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x00FF00;
                if(o==player1)
                {
                    g->pushlist();
                    g->background(0x808080, 3);
                }
                g->text(colorname(o), status);
                if(o==player1) g->poplist();
            }
            g->poplist();

            g->pushlist();
            g->strut(4);
            g->text("ping", 0xA0A0A0);
            loopv(spectators) g->textf("%.1f", 0xFFFFFF, NULL, NULL, spectators[i]->ping);
            g->poplist();

            g->pushlist();
            g->strut(4);
            g->text("cn", 0xA0A0A0);
            loopv(spectators) g->textf("%d", 0xFFFFFF, NULL, NULL, spectators[i]->clientnum);
            g->poplist();

            if(mastip)
            {
                g->pushlist();
                g->strut(7);
                g->text("ip", 0xA0A0A0);
                loopv(spectators)
                    g->textf("%s", 0xFFFFFF, NULL, NULL, GeoIP_num_to_addr(endianswap(spectators[i]->extdata.data.ip)));
                g->poplist();
            }

            if(mastkick)
            {
                g->pushlist();
                g->text("kick", 0xA0A0A0);
                g->strut(5);
                loopv(spectators)
                {
                    if(g->button("kick", 0xFF0000, NULL)&G3D_UP)
                        kick(spectators[i]->name, "");
                }
                g->poplist();
            }

            if(mastspec)
            {
                g->pushlist();
                g->text("spec", 0xA0A0A0);
                g->strut(6);
                loopv(spectators)
                    if(g->button(tempformatstring("%s", spectators[i]->state==CS_SPECTATOR ? "unspec" : "spec"), 0xFF0000, NULL)&G3D_UP)
                        togglespectator(spectators[i]->state==CS_SPECTATOR ? 0 : 1, spectators[i]->name);
                g->poplist();
            }

            if(mastteams)
            {
                g->pushlist();
                g->text("teams", 0xA0A0A0);
                g->strut(6);
                loopv(spectators)
                    if(g->button(tempformatstring("\f%d%s", !strcmp(spectators[i]->team, "good") ? 0 : 3, spectators[i]->team), 0xFFFFFF, NULL)&G3D_UP)
                        setteam(spectators[i]->name, !strcmp(spectators[i]->team, "good") ? "evil" : "good");
                g->poplist();
            }

            if(mastmaster)
            {
                g->pushlist();
                g->text("master", 0xA0A0A0);
                g->strut(7);
                loopv(spectators)
                    if(g->button(tempformatstring("%s", spectators[i]->privilege>=PRIV_MASTER ? "take" : "give"), spectators[i]->privilege>=PRIV_MASTER ? 0xFF0000 : 0x00FF00, NULL)&G3D_UP)
                        setmaster(spectators[i]->privilege>=PRIV_MASTER ? "0" : "1", spectators[i]->name);
                g->poplist();
            }

            g->poplist();
        }
        g->allowautotab(true);
    }
}

