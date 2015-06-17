#include "engine.h"

extern selinfo sel;
extern void reorient();
extern void boxs3D(const vec &o, vec s, int g);
extern int gridsize;
extern int orient;
extern bool havesel;

vector<iteminfo> library;

extern bool isselinsel(selinfo &sel_in, selinfo &sel_wrap);

#ifndef M_PI_2
#define M_PI_2 M_PI*2
#endif // M_PI_2

#define looplibitems()           loopv(library)
#define loopselitems(sel)        looplibitems() if(isselinsel(library[i].sel, sel))
#define loopitemlinks()          looplibitems() loopvj(library[i].links)
#define loopselitemlinks(sel)    loopitemlinks() if(isselinsel(library[i].links[j].sel, sel))

#define loopitemilinks(i)        loopvj(library[i].links)
#define looplibitemsrev()	     loopvrev(library)
#define loopitemlinksrev()	     looplibitemsrev() loopvjrev(library[i].links)
#define loopselitemlinksrev(sel) loopitemlinksrev() if(isselinsel(library[i].links[j].sel, sel))

int linkcopygrid;
vector<linkinfo> linkcopys;

void decomptm(matrix3x3 tm,ivec &rot,ivec &flip)
{
	rot = ivec(0,0,0);
	if(fabs(tm.b.y) && fabs(tm.a.z))
        rot.y = 1;
	else if(fabs(tm.b.z))
	{
		rot.x = 1;
		if(fabs(tm.a.y)) rot.y = 1;
	}
	else if(fabs(tm.b.x))
	{
		rot.z = 1;
		if(fabs(tm.c.y)) rot.y = 1;
	}

	matrix3x3 m;

	m = matrix3x3(vec(1, 0, 0), vec(0, 1, 0), vec(0, 0, 1));
	m.rotate(-M_PI_2*rot.x, vec(1, 0, 0));
	tm.mul(tm,m);

	m = matrix3x3(vec(1, 0, 0), vec(0, 1, 0), vec(0, 0, 1));
	m.rotate(-M_PI_2*rot.z, vec(0, 0, 1));
	tm.mul(tm, m);

	m = matrix3x3(vec(1, 0, 0), vec(0, 1, 0), vec(0, 0, 1));
	m.rotate(-M_PI_2*rot.y, vec(0, 1, 0));
	tm.mul(tm, m);

	flip = ivec(
		tm.a.x<0 ? -1 : 1,
		tm.b.y<0 ? -1 : 1,
		tm.c.z<0 ? -1 : 1
	);
}

void selrotate(selinfo &sel,selinfo &trans,int cw,int d)
{
	int dd = (cw<0)==dimcoord(trans.orient) ? R[d] : C[d];
	float mid = trans.s[dd]*trans.grid/2+trans.o[dd];
	vec s(trans.o.v);
	vec r = vec(
		sel.s.x*sel.grid/2,
		sel.s.y*sel.grid/2,
		sel.s.z*sel.grid/2
	);
	vec e = sel.o.tovec();
	e.add(r);

	e[dd] -= (e[dd]-mid)*2;
	e.sub(s);
	swap(e[R[d]], e[C[d]]);
	e.add(s);

	swap(r[R[d]], r[C[d]]);
	e.sub(r);
	sel.o = e;
	swap(sel.s[R[d]], sel.s[C[d]]);
}
