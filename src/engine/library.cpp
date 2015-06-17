#include "engine.h"
#include "library.h"

void libitem(const char *name)
{
	library.add(iteminfo(sel, name));
	conoutf("The selection is stored in the library under id %i.", library.length()-1);
}
COMMAND(libitem,"s");

void dellibitem(int *i)
{
	library.remove(*i);
}
COMMAND(dellibitem,"i");

void getitemname(int i, const char *name)
{
	if(strlen(library[i].name)==0)
        formatstring(name)("libitem %i", i);
	else
        formatstring(name)("%s", library[i].name);
}
ICOMMAND(getlibitemname, "i", (int *i), string name; getitemname(*i,name); result(name); );

void setlibitemname(int *i, const char *name_)
{
	library[*i].name = newstring(name_);
}
COMMAND(setlibitemname, "is");

void itemrotate(selinfo &itemsel, selinfo &trans, ivec &rot, bool back = false)
{
	if(back)
	{
		if(rot.x!=0) { trans.orient = 0; mprotate(-1, trans, true); selrotate(itemsel, trans, -1, 0); }
		if(rot.z!=0) { trans.orient = 4; mprotate(-1, trans, true); selrotate(itemsel, trans, -1, 2); }
		if(rot.y!=0) { trans.orient = 2; mprotate(-1, trans, true); selrotate(itemsel, trans, -1, 1); }
	}
	else
	{
		if(rot.y!=0) { trans.orient = 2; mprotate(1, trans, true); selrotate(itemsel, trans, 1, 1); }
		if(rot.z!=0) { trans.orient = 4; mprotate(1, trans, true); selrotate(itemsel, trans, 1, 2); }
		if(rot.x!=0) { trans.orient = 0; mprotate(1, trans, true); selrotate(itemsel, trans, 1, 0); }
	}
}

void itemflip(selinfo &itemsel,ivec &flip)
{
	loopi(3)
	{
		if(flip[i]==-1)
        {
			itemsel.orient = i*2;
			mpflip(itemsel, true);
		}
	}
}

void itemcopy(selinfo &sel)
{
	mpcopy(localedit, sel, true, linkcopys, linkcopygrid);
	linkcopys.shrink(0);
	linkcopygrid = 0;
}

void itemcopy(int *i)
{
	itemcopy(library[*i].sel);
}

void itemcopy(int *i, matrix3x3 &tm)
{
	ivec rot = ivec();
	ivec flip = ivec();
	decomptm(tm, rot, flip);

	itemflip(library[*i].sel, flip);
	itemrotate(library[*i].sel, library[*i].trans, rot);

	itemcopy(library[*i].sel);

	itemrotate(library[*i].sel, library[*i].trans, rot, true);
	itemflip(library[*i].sel, flip);
}

void itempaste(int *i, int *j)
{
	mppaste(localedit, library[*i].links[*j].sel, true, linkcopys, linkcopygrid);
}

void applyitemlink(int *i, int *j)
{
	itemcopy(i, library[*i].links[*j].tm);
	itempaste(i, j);
}
COMMAND(applyitemlink,"ii");

void applylibitem(int *i)
{
	if(*i < 0 || *i+1 > library.length()) return;
	loopitemilinks(*i) applyitemlink(i,&j);
}
COMMAND(applylibitem,"i");

void applylib()
{
	looplibitems() applylibitem(&i);
}
COMMAND(applylib,"");

void selectlibitem(int *i)
{
	sel = library[*i].sel;
	gridsize = library[*i].sel.grid;
	orient = library[*i].sel.orient;
	havesel = true;
	reorient();
}
COMMAND(selectlibitem,"i");

void itemlink(int *i)
{
	if(*i<0 || *i+1>library.length()) return;
	itemcopy(i);
	mppaste(localedit, sel, true, linkcopys, linkcopygrid);
	library[*i].addlink(sel);
	reorient();
}
COMMAND(itemlink,"i");

void itemlinkdelete(selinfo &sel)
{
	loopselitemlinksrev(sel) library[i].clearlink(j);
}

void itemlinkcopy(vector <linkinfo> &linkcopys, selinfo &sel, int &linkcopygrid)
{
	linkcopys.setsize(0);
	linkcopygrid = sel.grid;
	loopselitemlinks(sel) linkcopys.add(library[i].links[j]).sel.o.sub(sel.o);
}

void itemlinkpaste(vector <linkinfo> &linkcopys, selinfo &sel, int &linkcopygrid)
{
	float m = float(sel.grid)/float(linkcopygrid);
	loopv(linkcopys)
	{
		selinfo nsel = linkcopys[i].sel;
		vec o = nsel.o.tovec();
		o.mul(m).add(sel.o.tovec());
		nsel.o = ivec(o);
		nsel.grid *= m;
		if(nsel.grid>=1) linkcopys[i].parent->addlink(nsel,linkcopys[i].tm);
	}
}

void itemlinkrotate(int cw, selinfo &sel)
{
	int d = dimension(sel.orient);
	int scw = !dimcoord(sel.orient) ? -cw : cw;

	vec axis = vec(0, 0, 0);
	axis[d] = 1;
	matrix3x3 m = matrix3x3(vec(1, 0, 0),vec(0, 1, 0),vec(0, 0, 1));
	m.rotate(0,-cw, axis);
	loopselitemlinks(sel)
	{
		library[i].links[j].tm.mul(library[i].links[j].tm, m);
		selrotate(library[i].links[j].sel, sel, scw, d);
	}
}

void itemlinkflip(selinfo &sel)
{
	int d = dimension(sel.orient);
	matrix3x3 m = matrix3x3(
		vec(d==0 ? -1 : 1,             0, 0),
		vec(            0, d==1 ? -1 : 1, 0),
		vec(            0,             0, d==2 ? -1 : 1)
	);
	loopselitemlinks(sel)
	{
		library[i].links[j].tm.mul(library[i].links[j].tm, m);
		library[i].links[j].sel.o.v[d] = sel.o.v[d] + sel.s.v[d]*sel.grid - library[i].links[j].sel.s.v[d]*library[i].links[j].sel.grid - library[i].links[j].sel.o.v[d] + sel.o.v[d];
	}
}

void selectitemlink(int *i,int *j)
{
	sel = library[*i].links[*j].sel;
	gridsize = library[*i].links[*j].sel.grid;
	orient = library[*i].links[*j].sel.orient;
	havesel = true;
	reorient();
}
COMMAND(selectitemlink,"ii");

void rendertxtstart()
{
	defaultshader->set();
	glEnable(GL_TEXTURE_2D);
}

void rendertxtstop()
{
	glDisable(GL_TEXTURE_2D);
	notextureshader->set();
}

void render_text(const char *str, vec& o, int r = 255, int g = 100, int b = 0, int a = 200)
{
	rendertxtstart();

	float yaw = atan2f(o.y-camera1->o.y, o.x-camera1->o.x);
	glPushMatrix();
	glTranslatef(o.x, o.y, o.z);
	glRotatef(yaw/RAD-90, 0, 0, 1);
	glRotatef(-90, 1, 0, 0);
	glScalef(-0.03, 0.03, 0.03);

	int tw, th;
	text_bounds(str, tw, th);
	draw_text(str, -tw/2, -th, r, g, b, 200);

	glPopMatrix();
	rendertxtstop();
}

void itembox3d(selinfo& sel,const char *str, int r = 255, int g = 100, int b = 0, int a = 200, float off = 0.2f)
{
	vec size = sel.s.tovec().mul(sel.grid).add(2*off);
	vec o = sel.o.tovec().sub(off);
	glColor4ub(r, g, b, a);
	boxs3D(o, size, 1);
	glColor4ub(r, g, b,int(a/3));
	boxs3D(o, vec(1).mul(sel.grid).add(2*off), 1);
	o.add(vec(size.x/2, size.y/2, size.z));
	render_text(str, o, r, g, b, a);
}
void renderlib()
{
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);

	looplibitems()
	{
		string ititle;
		getitemname(i,ititle);
		itembox3d(library[i].sel, ititle);

		itembox3d(library[i].trans, "", 255, 0, 0,100, 0.3f);

		loopitemilinks(i)
		{
			defformatstring(linktitle)("%s:%i",ititle,j);
			itembox3d(library[i].links[j].sel, linktitle, 0, 255, 100);
		}
	}

	glDisable(GL_BLEND);
}

void save_sel(stream *f,selinfo &sel)
{
	defformatstring(str)("%d %d %d  %d %d %d  %d %d",
		sel.s.x,sel.s.y,sel.s.z,
		sel.o.x,sel.o.y,sel.o.z,
		sel.grid,sel.orient
	);
	f->putline(str);
}

void save_tm(stream *f,matrix3x3 &tm)
{
	defformatstring(str)("%.0f %.0f %.0f  %.0f %.0f %.0f  %.0f %.0f %.0f",
		tm.a.x,tm.a.y,tm.a.z,
		tm.b.x,tm.b.y,tm.b.z,
		tm.c.x,tm.c.y,tm.c.z
	);
	f->putline(str);
}

bool save_library(string &libname)
{
	stream *f = opengzfile(libname, "wb");
	if(!f) { conoutf(CON_WARN, "could not write lib to %s", libname); return false; }
	f->putlil<int>(library.length());
	f->putchar('\n');
	looplibitems()
	{
		save_sel(f, library[i].sel);
		conoutf("%s",library[i].name);
		f->putline(library[i].name);
		f->putlil<int>(library[i].links.length());
		f->putchar('\n');
		loopitemilinks(i)
		{
			save_sel(f, library[i].links[j].sel);
			save_tm(f,library[i].links[j].tm);
		}
	}
	delete f;
	conoutf("wrote map file %s", libname);
	return true;
}

selinfo load_sel(stream *f)
{
	selinfo s = selinfo();
	char buf[512];
	f->getline(buf, sizeof(buf));
	sscanf(buf,"%d %d %d  %d %d %d  %d %d",
		  &s.s.x,&s.s.y,&s.s.z,
		  &s.o.x,&s.o.y,&s.o.z,
		  &s.grid,&s.orient
	);
	return s;
}
matrix3x3 load_tm(stream *f)
{
	matrix3x3 m = matrix3x3();
	char buf[512];
	f->getline(buf, sizeof(buf));
	sscanf(buf,"%f %f %f  %f %f %f  %f %f %f",
		&m.a.x,&m.a.y,&m.a.z,
		&m.b.x,&m.b.y,&m.b.z,
		&m.c.x,&m.c.y,&m.c.z
	);
	return m;
}

bool load_library(string &libname)
{
	library.setsize(0);
	stream *f = opengzfile(libname, "rb");
	if(!f) { conoutf(CON_ERROR, "could not read lib %s", libname); return false; }
	int ilen = f->getlil<int>();
	f->getchar();
	loopi(ilen)
	{
		library.add(iteminfo(load_sel(f)));

		char buf[512];
		f->getline(buf,sizeof(buf));
		int blen = strlen(buf);
		if(blen > 0 && buf[blen-1] == '\n'){ buf[blen-1]=0; library[i].name = newstring(buf); }

		int llen = f->getlil<int>();
		f->getchar();
		loopj(llen)
		{
			library[i].addlink(load_sel(f));
			library[i].links[j].tm = load_tm(f);
		}
	}
	delete f;
	conoutf("read lib %s", libname);
	return true;
}

void getlibitemslist()
{
	vector<char> buf;
	string item;
	looplibitems()
	{
		formatstring(item)("\"libitem %i\"", i);
        buf.put(item, strlen(item));
	}
	buf.add('\0');
	result(buf.getbuf());
}

ICOMMAND(libitems, "", (), getlibitemslist());
ICOMMAND(libitemscnt, "", (), intret(library.length()));
ICOMMAND(linkscnt, "i", (int *i), intret(library[*i].links.length()));
