struct favserver
{
    char *name;
    char *address;
    char *realaddress;
    int port;
	~favserver()
	{
		DELETEA(name)
		DELETEA(address)
		DELETEA( realaddress)
	}
};

struct favservergroup
{
    char *headline;
    vector<favserver *> server;
	~favservergroup()
	{
		DELETEA(headline)
		server.shrink(0);
	}
};
vector<favservergroup *> favs;

favserver *getfavserver(int group, int server)
{
	if(!favs.inrange(group) || !favs[group] ) return NULL;

	favservergroup *g = favs[group];
	if(!g->server.inrange(server)) return NULL;

	return g->server[server];
}

void favgroup(const char *headline)
{
	if(!headline || !headline[0]) return;

    favservergroup *fsg = new favservergroup;
	fsg->headline = new char[21];
    strncpy(fsg->headline, headline, 21);
    fsg->headline[20] = '\0';
    favs.add(fsg);
}

void favserv(const char *name, const char *address, int port)
{
    if(!favs.length()) return;

    favservergroup *fsg = favs.last();
	if(!name || !address || !port || !name[0] || !address[0]) return;

    favserver *fs = new favserver;
	fs->name = new char[21];
    strncpy(fs->name, name, 21);
    fs->name[20] = '\0';
	fs->address = new char[16];
    strncpy(fs->address, address, 16);
    fs->address[15] = '\0';
    fs->port = port;
    fsg->server.add(fs);
    addserver(address, port);
}

void clearfavs()
{
	loopv(favs) DELETEP( favs[i])
    favs.setsize(0);
}

ICOMMAND(favserv, "ssi", (const char *name, const char *address, int *port), favserv(name, address, *port));
ICOMMAND(favgroup, "s" ,(const char *headline), favgroup(headline));
COMMAND(clearfavs, "");
ICOMMAND(numfavgroups, "", (), { intret(favs.length()); });
ICOMMAND(numfavserver, "i", (int *group),
         {
             if(favs.inrange(*group) && favs[*group]) intret(favs[*group]->server.length());
         });

const char *getfavservername(int group, int server)
{
	favserver *s = getfavserver(group, server);
	if(!s) return NULL;
	return s->name;
}

const char *getfavserveraddress(int group, int server)
{
	favserver *s = getfavserver(group, server);
	if(!s) return NULL;
	return s->address;
}

void getfavserverport(int group, int server)
{
	favserver *s = getfavserver(group, server);
	if(!s) return;
	intret(s->port);
}

void getfavgroupname(int group)
{
	if(!favs.inrange(group) || !favs[group]) return;
	if(favs[group]->headline) result(favs[group]->headline);
}

ICOMMAND(getfavservername, "ii", (int *group, int *server), result(getfavservername(*group, *server)));
ICOMMAND(getfavserveraddress, "ii", (int *group, int *server), result(getfavserveraddress(*group, *server)));
ICOMMAND(getfavserverport, "ii", (int *group, int *server), getfavserverport(*group, *server));
ICOMMAND(getfavgroupname, "i", (int *group), getfavgroupname(*group));

serverinfo *showfavs(g3d_gui *cgui)
{
    serverinfo *sc = NULL;
    cgui->pushlist();
    cgui->spring();
    loop(num, favs.length())
    {
        cgui->pushlist();

        cgui->text(favs[num]->headline, 0xCC44CC, NULL); // TODO: move to the middle of the list

        loop(serv, favs[num]->server.length())
        {
            favserver *fs = getfavserver(num, serv);
			if(!fs) continue;
            loopvk(servers)
            {
                serverinfo &so = *servers[k];
                if(strstr(so.name, fs->address) && so.port == fs->port)
                {
                    string plpl;
                    if(so.attr.inrange(3)) formatstring(plpl) ("%d/%d", so.numplayers, so.attr[3]);
                    else formatstring(plpl) ("--/--");
                    if(cgui->buttonf("%s %s(%s)", 0xCCCCCC, "menu", NULL,
                                     fs->name,
                                     so.numplayers && so.attr.inrange(3) ? (so.numplayers >= so.attr[3] ? "\f3": "\f2") : "\f4",
                                     plpl)&G3D_UP)
                    {
                        sc = &so;
                    }
                }
            }
        }
        cgui->poplist();
        cgui->space(5);
    }
    cgui->spring();
    cgui->poplist();
    cgui->separator();
    return sc;
}

void savefavoriteservercfg()
{
    stream *f = openutf8file(path(newstring("saved/favservs.cfg")), "w");
    if(!f) return;

    f->putline("// favgroup is the headline, favserver adds a new server to the group..\n");
    f->putline("clearfavs\n");
    loopv(favs)
    {
        f->printf("\nfavgroup \"%s\"\n", favs[i]->headline);
        loopvj(favs[i]->server) f->printf("favserv \"%s\" %s %d\n", favs[i]->server[j]->name,
                                                                    favs[i]->server[j]->address,
                                                                    favs[i]->server[j]->port);
    }
    delete f;
}
