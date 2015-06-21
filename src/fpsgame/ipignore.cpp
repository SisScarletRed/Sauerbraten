#include "game.h"
#include "ipbuf.h"
#include "event.h"

const char *escapecubescriptstring(const char *str, strtool &v, bool quotes = true)
{
    if(quotes)
        v.add('"');

    // same as escapestring() from command.cpp but thread safe
    for(const char *s = str; *s; s++) switch(*s)
    {
        case '\n': v.append("^n", 2); break;
        case '\t': v.append("^t", 2); break;
        case '\f': v.append("^f", 2); break;
        case '"': v.append("^\"", 2); break;
        case '^': v.append("^^", 2); break;
        default: v.add(*s); break;
    }

    if(quotes)
        v.add('"');

    return v.str();
}

using namespace game;

namespace ipignore
{
    #define CFG_FILE "ipignore.cfg"
    #define LOG_FILE "ipignore.log"

    static ipbuf ips;
    static stream *log = NULL;

    struct ipignore
    {
        string reason;
        uint hits;
        time_t lasthit;

        void hit()
        {
            lasthit = time(NULL);
            hits++;
        }

        ipignore(const char *reason_in, uint hits, uint lasthit) : hits(hits), lasthit(lasthit)
        {
            if(reason_in && *reason_in)
                copystring(reason, reason_in);
            else
                copystring(reason, "-");
        }
    };

    static void freeipignore(void *data)
    {
        ipignore *ipignoredata = (ipignore*)data;
        delete ipignoredata;
    }

    static int getcn(const char *msg)
    {
        if(!isnumeric(msg))
            return -1;

        int cn = atoi(msg);

        if(cn >= 0 && cn < MAXCLIENTS)
            return cn;

        return -1;
    }

    static void addip(const char *address, const char *reason = NULL, uint *hits = NULL, uint *lasthit = NULL)
    {
        if(!*address)
        {
            conoutf(CON_ERROR, "invalid ip address");
            return;
        }

        ipmask ip;

        bool ok;
        int cn;

        if((cn = getcn(address)) != -1)
        {
            fpsent *d;

            if(!(d = game::getclient(cn)))
            {
                conoutf(CON_ERROR, "invalid client number");
                return;
            }

            if(!d->extdatawasinit)
            {
                conoutf(CON_ERROR, "unable to get ip address of given player");
                return;
            }

            ip.ip = d->extdata.data.ip;
            ip.setbitcount(24);
            ok = true;
        }
        else
        {
            ip.parse(address);
            ok = ip.ok();
        }

        if(!ok)
        {
            conoutf(CON_ERROR, "invalid ip address");
            return;
        }

        int bitcount;
        if((bitcount = ip.getbitcount()) < 8)
        {
            conoutf(CON_ERROR, "min netmask is 8");
            return;
        }
        else if(bitcount > 24)
            ip.setbitcount(24);

        if(ips.findip(ip.ip))
        {
            string ipstr;
            ip.print(ipstr);
            conoutf(CON_ERROR, "ip address (%s) conflicts with already added ip address", ipstr);
            return;
        }

        ips.addip(ip, 0, new ipignore(reason, hits ? *hits : 0, lasthit ? *lasthit : 0), freeipignore);

        string ipstr;
        ip.print(ipstr);
        conoutf("ip address (%s) added to ignore list", ipstr);
    }

    static void delip(const char *address)
    {
        if(!*address)
        {
            conoutf(CON_ERROR, "no ip address given");
            return;
        }

        ipmask ip(address);

        if(!ip.ok())
        {
            conoutf(CON_ERROR, "invalid ip address given");
            return;
        }

        if(ip.getbitcount() < 8)
        {
            conoutf(CON_ERROR, "min netmask is 8");
            return;
        }

        vector<ipmask> removedips;
        uint count;

        if((count = ips.delip(ip.ip, removedips)))
        {
            conoutf("removed %u ip%s from ignore list", count, plural(count));
            loopv(removedips)
            {
                auto &removedip = removedips[i];
                string ipstr;
                removedip.print(ipstr);
                conoutf("%d. %s", i+1, ipstr);
            }
        }
        else
            conoutf("no matching ip address found in ignore list");
    }

    static bool showignoredip(ipmask &ip, uint datatype, void *data, void *cbdata)
    {
        ipignore *ipignoredata = (ipignore*)data;
        int &counter = *(int*)cbdata;

        string ipstr;
        string lasthit;

        if(ipignoredata->lasthit)
        {
            tm *tm = localtime(&ipignoredata->lasthit);

            if(tm)
                strftime(lasthit, sizeof(string), "%c", tm);
            else
                goto error;
        }
        else
        {
            error:;
            copystring(lasthit, "-");
        }

        if(!cbdata) // push to cubescript
        {
            ip.print(ipstr);
            event::run(event::IPIGNORELIST);
            return true;
        }

        counter++;

        ip.print(ipstr);
        conoutf("%d. ip address: %s  hits: %u  last hit: %s  reason: %s", counter,
                ipstr, ipignoredata->hits, lasthit, ipignoredata->reason);

        return true;
    }

    static void ignorelist()
    {
        if(ips.isempty_())
        {
            conoutf("ip ignore list is empty");
            return;
        }

        conoutf("ignoring %u ip%s", (uint)ips.size(), plural(ips.size()));

        int counter = 0;
        ips.loopips(showignoredip, &counter);
    }

    static void ignorelistcubescript()
    {
        ips.loopips(showignoredip, NULL);
        event::run(event::IPIGNORELIST);
    }

    QICOMMAND(ipignore, "ignore an ip", "addr,reason,hits,lasthit", "ssii", (const char *addr, const char *reason, uint *hits, uint *lasthit), addip(addr, reason, hits, lasthit));
    ICOMMAND(delipignore, "s", (const char *addr), delip(addr));
    ICOMMAND(listipignore, "", (), ignorelist());
    ICOMMAND(listipignorecubescript, "", (), ignorelistcubescript());

    bool checkaddress(const char *address, bool remove)
    {
        if(getcn(address) != -1)
            return false;

        ipmask ip(address);

        if(!ip.ok())
            return false;

        if(!remove)
            addip(address);
        else
            delip(address);

        return true;
    }

    bool isignored(int cn, const char *text)
    {
        fpsent *d = game::getclient(cn);
        ipmask ip;

        if(!d)
            return false;

        ip.ip = d->extdata.data.ip;
        ip.setbitcount(24);

        if(game::isignored(d->clientnum))
        {
            if(text)
                goto isignored; // log text
            else
                return true;
        }

        void *ignoredata;

        if(ips.findip(d->extdata.data.ip, NULL, &ignoredata))
        {
            if(!text)
                return true;

            ((ipignore*)ignoredata)->hit();

            isignored:;

            if(!log)
                return true;

            string ipstr;
            ip.print(ipstr);

            string timebuf;
            time_t t = time(NULL);
            strftime(timebuf, sizeof(timebuf), "%c", localtime(&t));

            log->printf("[%s] ignored (%s) (%d) (%s): %s\n",
                        timebuf, d->name,
                        d->clientnum,
                        ipstr, text);

            return true;
        }

        return false;
    }

    bool isignored(uint ip)
    {
        return !!ips.findip(ip);
    }

    void startup()
    {
        execfile(CFG_FILE, false);
        log = openutf8file(LOG_FILE, "wb");

        if(!log)
            conoutf(CON_WARN, "couldn't open %s for writing", LOG_FILE);
    }

    void shutdown()
    {
        vector<ipaddr*> ipaddresses;
        ips.getallips(ipaddresses);

        stream *f = openutf8file(CFG_FILE, "wb");

        if(!f)
        {
            conoutf(CON_WARN, "couldn't open %s for writing", CFG_FILE);

            if(log)
                delete log;

            return;
        }

        f->printf("// automatically written on exit\n");

        loopv(ipaddresses)
        {
            auto ipaddr = ipaddresses[i];
            string ipstr;
            ipignore *ipignoredata = (ipignore*)ipaddr->data;
            strtool buf1, buf2;

            ipaddr->ip.print(ipstr);
            f->printf("ipignore %s %s %u %d\n",
                      escapecubescriptstring(ipstr, buf1),
                      escapecubescriptstring(ipignoredata->reason, buf2),
                      ipignoredata->hits, (int)ipignoredata->lasthit);
        }

        delete f;

        if(log)
            delete log;
    }

} // namespace ipignore
