// main.cpp: initialisation & main loop

#include "engine.h"
#include "event.h"
#include "extinfo.h"
#include "ipignore.h"
#include "quedversion.h"
#include "extinfo.h"

ICOMMAND(getclientversion, "", (), result(getfullversionname()));
QICOMMAND(delfile, "delete the file (win only)", "file,msg", "si", (const char *file, int *msg),
{
#ifdef WIN32
    const char *filepath = (const char *)path((char *)file);
    int tmp = remove(filepath);
    if(msg || debugquality) conoutf("file %s deleted with status %d", file, tmp);
    intret(tmp);
#endif // WIN32
    conoutf("\f3error: function only available on windows yet");
    intret(-1);
});

extern void cleargamma();

void cleanup()
{
    recorder::stop();
    cleanupserver();
    SDL_ShowCursor(1);
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    cleargamma();
    freeocta(worldroot);
    extern void clear_command(); clear_command();
    extern void clear_console(); clear_console();
    extern void clear_mdls();    clear_mdls();
    extern void clear_sound();   clear_sound();
    closelogfile();
    SDL_Quit();
}

const char *restart_string = "start sauerbraten.bat";
void quit(bool restart = false)  // normal exit
{
    event::run(event::SHUTDOWN);
    ipignore::shutdown();
    game::writestats();
    whois::writewhoisdb();
    extern void writeinitcfg();
    writeinitcfg();
    writeservercfg();
    writehistory();
    abortconnect();
    disconnect();
    localdisconnect();
    writecfg();
    cleanup();
    if(restart) system(restart_string);
    exit(EXIT_SUCCESS);
}
ICOMMAND(restart, "", (), quit(true));

void fatal(const char *s, ...)    // failure exit
{
    static int errors = 0;
    errors++;

    if(errors <= 2) // print up to one extra recursive error
    {
        char msg[2048];
        va_list vargs;
        va_start(vargs, s);
        vsnprintf(msg, sizeof(msg), s, vargs);
        va_end(vargs);
        logoutf("%s", msg);

        if(errors <= 1) // avoid recursion
        {
            if(SDL_WasInit(SDL_INIT_VIDEO))
            {
                SDL_ShowCursor(1);
                SDL_WM_GrabInput(SDL_GRAB_OFF);
                cleargamma();
            }
            #ifdef WIN32
                MessageBox(NULL, msg, "Cube 2: Sauerbraten fatal error", MB_OK|MB_SYSTEMMODAL);
            #endif
            SDL_Quit();
        }
    }

    _exit(EXIT_FAILURE);
}

SDL_Surface *screen = NULL;

RVAR0(curtime);
RVAR(lastmillis, 1);
RVAR0(elapsedtime);
RVAR(totalmillis, 1);
RVAR0(totalmillisrepeats);

dynent *player = NULL;

int initing = NOT_INITING;

bool initwarning(const char *desc, int level, int type)
{
    if(initing < level)
    {
        addchange(desc, type);
        return true;
    }
    return false;
}

#define SCR_MINW 320
#define SCR_MINH 200
#define SCR_MAXW 10000
#define SCR_MAXH 10000
#define SCR_DEFAULTW 1024
#define SCR_DEFAULTH 768
VARF(scr_w, SCR_MINW, -1, SCR_MAXW, initwarning("screen resolution"));
VARF(scr_h, SCR_MINH, -1, SCR_MAXH, initwarning("screen resolution"));
VARF(colorbits, 0, 0, 32, initwarning("color depth"));
VARF(depthbits, 0, 0, 32, initwarning("depth-buffer precision"));
VARF(stencilbits, 0, 0, 32, initwarning("stencil-buffer precision"));
VARF(fsaa, -1, -1, 16, initwarning("anti-aliasing"));
VARP(vsync, 0, 0, 2);

void writeinitcfg()
{
    stream *f = openutf8file("init.cfg", "w");
    if(!f) return;
    f->printf("// automatically written on exit, DO NOT MODIFY\n// modify settings in game\n");
    extern int fullscreen;
    f->printf("fullscreen %d\n", fullscreen);
    f->printf("scr_w %d\n", scr_w);
    f->printf("scr_h %d\n", scr_h);
    f->printf("colorbits %d\n", colorbits);
    f->printf("depthbits %d\n", depthbits);
    f->printf("stencilbits %d\n", stencilbits);
    f->printf("fsaa %d\n", fsaa);
    extern int useshaders, shaderprecision, forceglsl;
    f->printf("shaders %d\n", useshaders);
    f->printf("shaderprecision %d\n", shaderprecision);
    f->printf("forceglsl %d\n", forceglsl);
    extern int soundchans, soundfreq, soundbufferlen;
    f->printf("soundchans %d\n", soundchans);
    f->printf("soundfreq %d\n", soundfreq);
    f->printf("soundbufferlen %d\n", soundbufferlen);
    delete f;
}

QICOMMAND(quit, "quits the game without asking", "", "", (), quit(false));

static void getbackgroundres(int &w, int &h)
{
    float wk = 1, hk = 1;
    if(w < 1024) wk = 1024.0f/w;
    if(h < 768) hk = 768.0f/h;
    wk = hk = max(wk, hk);
    w = int(ceil(w*wk));
    h = int(ceil(h*hk));
}

string backgroundcaption = "";
Texture *backgroundmapshot = NULL;
string backgroundmapname = "";
char *backgroundmapinfo = NULL;

void restorebackground()
{
    if(renderedframe) return;
    renderbackground(backgroundcaption[0] ? backgroundcaption : NULL, backgroundmapshot, backgroundmapname[0] ? backgroundmapname : NULL, backgroundmapinfo, true);
}

string backgroundimg;
string backgroundmap;

VARP(bg3d, 0, 1, 1);
VARP(guitime, 15, 45, 90);

int box_spin,
    boxtimer = 0;
bool boxisspinning,
     boxspinleft = false;
float box_scale = 0;

void startboxspin(bool left)
{
    if(!left)
    {
        boxspinleft = false;
        box_spin -= (90/guitime);
    }
    else
    {
        boxspinleft = true;
        box_spin += (90/guitime);
    }
    if(boxisspinning == false) boxisspinning = true;
}

void spinbox()
{
	if(!curtime) return;
    if(boxisspinning)
    {
        if(boxtimer < guitime) boxtimer++;
        else
        {
            boxisspinning = false;
            boxtimer = 0;
            box_scale = 0;
        }

        if(!boxspinleft) box_spin += (90/guitime);
        else box_spin -= (90/guitime);

        if(boxtimer && boxtimer <= (guitime/2)) box_scale -= 0.015f*(45.0f/guitime);
        else if(boxtimer >= (guitime/2)) box_scale += 0.015f*(45.0f/guitime);
        else box_scale = 0;

        if(boxtimer==guitime) box_scale = 0;
    }
    else
    {
        if(box_spin%90)
        {
            if(box_spin%90 <= 44) box_spin -= (90/guitime);
            else box_spin += (90/guitime);
        }
    }
}

VARP(backgroundcolor, 0, 0, 0xFFFFFF);
SVARP(backgroundimage, "background.png");

void renderbackground(const char *caption, Texture *mapshot, const char *mapname, const char *mapinfo, bool restore, bool force)
{
    if(!inbetweenframes && !force) return;

    stopsounds(); // stop sounds while loading

    int w = screen->w, h = screen->h;
    if(forceaspect) w = int(ceil(h*forceaspect));
    getbackgroundres(w, h);
    gettextres(w, h);

    if(bg3d)
    {
        if(caption)
        {
            int tw = text_width(caption);
            float tsz = 0.04f*min(w, h)/FONTH,
                  tx = 0.5f*(w - tw*tsz), ty = h - 0.075f*1.5f*min(w, h) - 1.25f*FONTH*tsz;
            glPushMatrix();
            glTranslatef(tx, ty, 0);
            glScalef(tsz, tsz, 1);
            draw_text(caption, 0, 0);
            glPopMatrix();
        }

        if(mapshot || mapname)
        {
            if(mapshot && mapshot!=notexture)
            {
                defformatstring(mshot)("<blur:4/3/0>packages/base/%s.jpg", mapname);
                copystring(backgroundimg, mshot);
            }
            else
            {
                copystring(backgroundimg, "");
                settexture(tempformatstring("<blur:4/3/0>data/%s", backgroundimage), 0);
                glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0, 0); glVertex2f(0, 0);
                glTexCoord2f(1, 0); glVertex2f(w, 0);
                glTexCoord2f(0, 1); glVertex2f(0, h);
                glTexCoord2f(1, 1); glVertex2f(w, h);
                glEnd();
            }
            if(mapname) copystring(backgroundmap, mapname);
            else copystring(backgroundmap, "");
        }

        if(!mainmenu) return;

        glClearColor(float((backgroundcolor>>16)&0xFF)/255.0f, float((backgroundcolor>>8)&0xFF)/255.0f, float((backgroundcolor)&0xFF)/255.0f, 1.0);

        glEnable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-1.5, 1.5, -1.2, 1.2, 1.5, 15);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glColor4f(1, 1, 1, 1);
        defaultshader->set();
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        spinbox();
        glTranslatef(0, 0, -6);

        glRotatef(box_spin, 0, 1, 0);
        glScalef(3.0f+box_scale, 2.44f+box_scale, 3.03f+box_scale);

        vec A = vec(-1, -1, -1), B = vec( -1, -1, 1), C = vec( 1, -1, 1), D = vec( 1, -1, -1),
            E = vec( 1,  1, -1), F = vec(  1,  1, 1), G = vec(-1,  1, 1), H = vec(-1,  1, -1);

        // TODO: random new pics
        settexture("data/bg/1.png", 0);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex3f(E.x, E.y, E.z);
        glTexCoord2f(0, 1); glVertex3f(D.x, D.y, D.z);
        glTexCoord2f(1, 1); glVertex3f(A.x, A.y, A.z);
        glTexCoord2f(1, 0); glVertex3f(H.x, H.y, H.z);
        glEnd();

        settexture("data/bg/2.png", 0);
        glBegin(GL_QUADS);
        glTexCoord2f(1, 0); glVertex3f(F.x, F.y, F.z);
        glTexCoord2f(0, 0); glVertex3f(G.x, G.y, G.z);
        glTexCoord2f(0, 1); glVertex3f(B.x, B.y, B.z);
        glTexCoord2f(1, 1); glVertex3f(C.x, C.y, C.z);
        glEnd();

        settexture("data/bg/3.png", 0);
        glBegin(GL_QUADS);
        glTexCoord2f(1, 0); glVertex3f(E.x, E.y, E.z);
        glTexCoord2f(0, 0); glVertex3f(F.x, F.y, F.z);
        glTexCoord2f(0, 1); glVertex3f(C.x, C.y, C.z);
        glTexCoord2f(1, 1); glVertex3f(D.x, D.y, D.z);
        glEnd();

        settexture("data/bg/4.png", 0);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 1); glVertex3f(A.x, A.y, A.z);
        glTexCoord2f(1, 1); glVertex3f(B.x, B.y, B.z);
        glTexCoord2f(1, 0); glVertex3f(G.x, G.y, G.z);
        glTexCoord2f(0, 0); glVertex3f(H.x, H.y, H.z);
        glEnd();

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D);
        if(!force) renderedframe = false;

        return;
    }

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    static int lastupdate = -1, lastw = -1, lasth = -1;
    if((renderedframe && !mainmenu && lastupdate != lastmillis) || lastw != w || lasth != h)
    {
        lastupdate = lastmillis;
        lastw = w;
        lasth = h;
    }
    else if(lastupdate != lastmillis) lastupdate = lastmillis;

    loopi(restore ? 1 : 3)
    {
        glColor3f(1, 1, 1);
        settexture(tempformatstring("%sdata/%s", force ? "" : "<blur:4/3/0>", backgroundimage), 0);
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(0, 0);
        glTexCoord2f(1, 0); glVertex2f(w, 0);
        glTexCoord2f(0, 1); glVertex2f(0, h);
        glTexCoord2f(1, 1); glVertex2f(w, h);
        glEnd();

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);
        float lh = 0.5f*min(w, h), lw = lh*2,
              lx = 0.5f*(w - lw), ly = 0.5f*(h*0.5f - lh);
        settexture("data/logo.png", 3);
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(lx,    ly);
        glTexCoord2f(1, 0); glVertex2f(lx+lw, ly);
        glTexCoord2f(0, 1); glVertex2f(lx,    ly+lh);
        glTexCoord2f(1, 1); glVertex2f(lx+lw, ly+lh);
        glEnd();

        if(mapshot || mapname)
        {
            if(mapshot && mapshot!=notexture)
            {
                defformatstring(mshot)("<blur:4/3/0>packages/base/%s.jpg", mapname);
                copystring(backgroundimg, mshot);
            }
            else
            {
                copystring(backgroundimg, "");
                settexture(tempformatstring("<blur:4/3/0>data/%s", backgroundimage), 0);
                glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0, 0); glVertex2f(0, 0);
                glTexCoord2f(1, 0); glVertex2f(w, 0);
                glTexCoord2f(0, 1); glVertex2f(0, h);
                glTexCoord2f(1, 1); glVertex2f(w, h);
                glEnd();
            }
            if(mapname) copystring(backgroundmap, mapname);
            else copystring(backgroundmap, "");
        }
        glDisable(GL_BLEND);
        if(!restore) swapbuffers(false);
    }
    glDisable(GL_TEXTURE_2D);

    if(!restore)
    {
        renderedframe = false;
        copystring(backgroundcaption, caption ? caption : "");
        backgroundmapshot = mapshot;
        copystring(backgroundmapname, mapname ? mapname : "");
        if(mapinfo != backgroundmapinfo)
        {
            DELETEA(backgroundmapinfo);
            if(mapinfo) backgroundmapinfo = newstring(mapinfo);
        }
    }
}

float loadprogress = 0;

bool isloading = false;
float circle[3];
VARP(loadingstyle, 1, 1, 2);

extern bool thisguiisshowing(const char *name);
extern bool showloadingscoreboard;

void renderprogress(float bar, const char *text, GLuint tex, bool background)   // also used during loading
{
    if(!inbetweenframes || envmapping) return;

    clientkeepalive();      // make sure our connection doesn't time out while loading maps etc.

    #ifdef __APPLE__
    interceptkey(SDLK_UNKNOWN); // keep the event queue awake to avoid 'beachball' cursor
    #endif

    extern int sdl_backingstore_bug;
    if(background || sdl_backingstore_bug > 0) restorebackground();

    int w = screen->w, h = screen->h;
    if(forceaspect) w = int(ceil(h*forceaspect));
    getbackgroundres(w, h);
    gettextres(w, h);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    defaultshader->set();
    glColor3f(1, 1, 1);

    if(!settexture(backgroundimg, 0)) settexture(tempformatstring("<blur:4/3/0>data/%s", backgroundimage), 0);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(1, 0); glVertex2f(w, 0);
    glTexCoord2f(1, 1); glVertex2f(w, h);
    glTexCoord2f(0, 1); glVertex2f(0, h);
    glEnd();

    switch(loadingstyle)
    {
        case 2:
        {
            glEnable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            loopi(3)
            {
                settexture(tempformatstring("data/circle%d.png", i+1));
                glPushMatrix();
                glTranslatef(w/2, h/2, 0);
                glRotatef(circle[i], 0.0f, 0.0f, 1.0f);
                glTranslatef(w/-2, h/-2, 0);
                glBegin(GL_QUADS);
                glTexCoord2f(0, 0); glVertex2f(w/2-300, h/2-300);
                glTexCoord2f(1, 0); glVertex2f(w/2+300, h/2-300);
                glTexCoord2f(1, 1); glVertex2f(w/2+300, h/2+300);
                glTexCoord2f(0, 1); glVertex2f(w/2-300, h/2+300);
                glEnd();
                glPopMatrix();
                if(circle[i] >= 360) circle[i] = 0;
                else circle[i] += i+1;
            }
            if(strcmp(backgroundmap, ""))
            {
                int wb, hb;
                glPushMatrix();
                glScalef(1/1.2f, 1/1.2f, 1);
                text_bounds(backgroundmap, wb, hb);
                int x = ((w/2)*1.2f)-wb/2;
                draw_text(strupr(backgroundmap), x, 0);
                glPopMatrix();
            }
            if(text)
            {
                int wb, hb;
                glPushMatrix();
                glScalef(1/3.2f, 1/3.2f, 1);
                text_bounds(text, wb, hb);
                int x = ((w/2)*3.2f)-wb/2,
                    y = (h/2)*3.2f;
                draw_text(text, x, y);
                glPopMatrix();
            }
            glDisable(GL_BLEND);
            glDisable(GL_TEXTURE_2D);
        }
        default:
        {
            glEnable(GL_TEXTURE_2D);
            defaultshader->set();

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor3f(0.5, 0.5, 0.5);
            float lh = 0.5f*min(w, h), lw = lh*2,
                  lx = 0.5f*(w - lw), ly = 0.5f*(h*0.5f - lh);
            settexture("data/logo.png", 3);
            glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0, 0); glVertex2f(lx,    ly);
            glTexCoord2f(1, 0); glVertex2f(lx+lw, ly);
            glTexCoord2f(0, 1); glVertex2f(lx,    ly+lh);
            glTexCoord2f(1, 1); glVertex2f(lx+lw, ly+lh);
            glEnd();
            glDisable(GL_BLEND);

            glColor3f(1, 1, 1);
            float fh = 0.075f*min(w, h), fw = fh*10,
                  fx = renderedframe ? w - fw - fh/4 : 0.5f*(w - fw),
                  fy = renderedframe ? fh/4 : h - fh*1.5f,
                  fu1 = 0/512.0f, fu2 = 511/512.0f,
                  fv1 = 0/64.0f, fv2 = 52/64.0f;
            settexture("data/loading_frame.png", 3);
            glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(fu1, fv1); glVertex2f(fx,    fy);
            glTexCoord2f(fu2, fv1); glVertex2f(fx+fw, fy);
            glTexCoord2f(fu1, fv2); glVertex2f(fx,    fy+fh);
            glTexCoord2f(fu2, fv2); glVertex2f(fx+fw, fy+fh);
            glEnd();

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            float bw = fw*(511 - 2*17)/511.0f, bh = fh*20/52.0f,
                  bx = fx + fw*17/511.0f, by = fy + fh*16/52.0f,
                  bv1 = 0/32.0f, bv2 = 20/32.0f,
                  su1 = 0/32.0f, su2 = 7/32.0f, sw = fw*7/511.0f,
                  eu1 = 23/32.0f, eu2 = 30/32.0f, ew = fw*7/511.0f,
                  mw = bw - sw - ew,
                  ex = bx+sw + max(mw*bar, fw*7/511.0f);

            if(bar > 0)
            {
                settexture("data/loading_bar.png", 3);
                glBegin(GL_QUADS);
                glTexCoord2f(su1, bv1); glVertex2f(bx,    by);
                glTexCoord2f(su2, bv1); glVertex2f(bx+sw, by);
                glTexCoord2f(su2, bv2); glVertex2f(bx+sw, by+bh);
                glTexCoord2f(su1, bv2); glVertex2f(bx,    by+bh);

                glTexCoord2f(su2, bv1); glVertex2f(bx+sw, by);
                glTexCoord2f(eu1, bv1); glVertex2f(ex,    by);
                glTexCoord2f(eu1, bv2); glVertex2f(ex,    by+bh);
                glTexCoord2f(su2, bv2); glVertex2f(bx+sw, by+bh);

                glTexCoord2f(eu1, bv1); glVertex2f(ex,    by);
                glTexCoord2f(eu2, bv1); glVertex2f(ex+ew, by);
                glTexCoord2f(eu2, bv2); glVertex2f(ex+ew, by+bh);
                glTexCoord2f(eu1, bv2); glVertex2f(ex,    by+bh);
                glEnd();
            }
            if(text)
            {
                int tw = text_width(text);
                float tsz = bh*0.8f/FONTH;
                if(tw*tsz > mw) tsz = mw/tw;
                glPushMatrix();
                glTranslatef(bx+sw, by + (bh - FONTH*tsz)/2, 0);
                glScalef(tsz, tsz, 1);
                draw_text(text, 0, 0);
                glPopMatrix();
            }
            glDisable(GL_BLEND);

            /* TODO: crashes serverbrowser-loading
            if(showloadingscoreboard)
            {
                if(!thisguiisshowing("scoreboardextinfo")) showgui("scoreboardextinfo");
                g3d_render();
            }*/

            glDisable(GL_TEXTURE_2D);
        }
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    swapbuffers(false);

    isloading = true;
}

void keyrepeat(bool on)
{
    SDL_EnableKeyRepeat(on ? SDL_DEFAULT_REPEAT_DELAY : 0,
                             SDL_DEFAULT_REPEAT_INTERVAL);
}

bool grabinput = false, minimized = false;

void inputgrab(bool on)
{
#ifndef WIN32
    if(!(screen->flags & SDL_FULLSCREEN)) SDL_WM_GrabInput(SDL_GRAB_OFF);
    else
#endif
    SDL_WM_GrabInput(on ? SDL_GRAB_ON : SDL_GRAB_OFF);
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

void setfullscreen(bool enable)
{
    if(!screen) return;
#if defined(WIN32) || defined(__APPLE__)
    initwarning(enable ? "fullscreen" : "windowed");
#else
    if(enable == !(screen->flags&SDL_FULLSCREEN))
    {
        SDL_WM_ToggleFullScreen(screen);
        inputgrab(grabinput);
    }
#endif
}

#ifdef _DEBUG
VARF(fullscreen, 0, 0, 1, setfullscreen(fullscreen!=0));
#else
VARF(fullscreen, 0, 1, 1, setfullscreen(fullscreen!=0));
#endif

void screenres(int *w, int *h)
{
#if !defined(WIN32) && !defined(__APPLE__)
    if(initing >= INIT_RESET)
    {
#endif
        scr_w = clamp(*w, SCR_MINW, SCR_MAXW);
        scr_h = clamp(*h, SCR_MINH, SCR_MAXH);
#if defined(WIN32) || defined(__APPLE__)
        initwarning("screen resolution");
#else
        return;
    }
    SDL_Surface *surf = SDL_SetVideoMode(clamp(*w, SCR_MINW, SCR_MAXW), clamp(*h, SCR_MINH, SCR_MAXH), 0, SDL_OPENGL|(screen->flags&SDL_FULLSCREEN ? SDL_FULLSCREEN : SDL_RESIZABLE));
    if(!surf) return;
    screen = surf;
    scr_w = screen->w;
    scr_h = screen->h;
    glViewport(0, 0, scr_w, scr_h);
#endif
}

COMMAND(screenres, "ii");

static int curgamma = 100;
VARFP(gamma, 30, 100, 300,
{
    if(gamma == curgamma) return;
    curgamma = gamma;
	float f = gamma/100.0f;
    if(SDL_SetGamma(f,f,f)==-1) conoutf(CON_ERROR, "Could not set gamma: %s", SDL_GetError());
});

void restoregamma()
{
    if(curgamma == 100) return;
    float f = curgamma/100.0f;
    SDL_SetGamma(1, 1, 1);
    SDL_SetGamma(f, f, f);
}

void cleargamma()
{
    if(curgamma != 100) SDL_SetGamma(1, 1, 1);
}

VARP(mainmenugamma, 0, 1, 1);

void maingamma()
{
    if(mainmenu || isloading) SDL_SetGamma(1, 1, 1);
    else
    {
        float f = curgamma/100.0f;
        SDL_SetGamma(f, f, f);
    }
}

VAR(dbgmodes, 0, 0, 1);

int desktopw = 0, desktoph = 0;

void setupscreen(int &usedcolorbits, int &useddepthbits, int &usedfsaa)
{
    int flags = SDL_RESIZABLE;
    #if defined(WIN32) || defined(__APPLE__)
    flags = 0;
    #endif
    if(fullscreen) flags = SDL_FULLSCREEN;
    SDL_Rect **modes = SDL_ListModes(NULL, SDL_OPENGL|flags);
    if(modes && modes!=(SDL_Rect **)-1)
    {
        int widest = -1, best = -1;
        for(int i = 0; modes[i]; i++)
        {
            if(dbgmodes) conoutf(CON_DEBUG, "mode[%d]: %d x %d", i, modes[i]->w, modes[i]->h);
            if(widest < 0 || modes[i]->w > modes[widest]->w || (modes[i]->w == modes[widest]->w && modes[i]->h > modes[widest]->h))
                widest = i;
        }
        if(scr_w < 0 || scr_h < 0)
        {
            int w = scr_w, h = scr_h, ratiow = desktopw, ratioh = desktoph;
            if(w < 0 && h < 0) { w = SCR_DEFAULTW; h = SCR_DEFAULTH; }
            if(ratiow <= 0 || ratioh <= 0) { ratiow = modes[widest]->w; ratioh = modes[widest]->h; }
            for(int i = 0; modes[i]; i++) if(modes[i]->w*ratioh == modes[i]->h*ratiow)
            {
                if(w <= modes[i]->w && h <= modes[i]->h && (best < 0 || modes[i]->w < modes[best]->w))
                    best = i;
            }
        }
        if(best < 0)
        {
            int w = scr_w, h = scr_h;
            if(w < 0 && h < 0) { w = SCR_DEFAULTW; h = SCR_DEFAULTH; }
            else if(w < 0) w = (h*SCR_DEFAULTW)/SCR_DEFAULTH;
            else if(h < 0) h = (w*SCR_DEFAULTH)/SCR_DEFAULTW;
            for(int i = 0; modes[i]; i++)
            {
                if(w <= modes[i]->w && h <= modes[i]->h && (best < 0 || modes[i]->w < modes[best]->w || (modes[i]->w == modes[best]->w && modes[i]->h < modes[best]->h)))
                    best = i;
            }
        }
        if(flags&SDL_FULLSCREEN)
        {
            if(best >= 0) { scr_w = modes[best]->w; scr_h = modes[best]->h; }
            else if(desktopw > 0 && desktoph > 0) { scr_w = desktopw; scr_h = desktoph; }
            else if(widest >= 0) { scr_w = modes[widest]->w; scr_h = modes[widest]->h; }
        }
        else if(best < 0)
        {
            scr_w = min(scr_w >= 0 ? scr_w : (scr_h >= 0 ? (scr_h*SCR_DEFAULTW)/SCR_DEFAULTH : SCR_DEFAULTW), (int)modes[widest]->w);
            scr_h = min(scr_h >= 0 ? scr_h : (scr_w >= 0 ? (scr_w*SCR_DEFAULTH)/SCR_DEFAULTW : SCR_DEFAULTH), (int)modes[widest]->h);
        }
        if(dbgmodes) conoutf(CON_DEBUG, "selected %d x %d", scr_w, scr_h);
    }
    if(scr_w < 0 && scr_h < 0) { scr_w = SCR_DEFAULTW; scr_h = SCR_DEFAULTH; }
    else if(scr_w < 0) scr_w = (scr_h*SCR_DEFAULTW)/SCR_DEFAULTH;
    else if(scr_h < 0) scr_h = (scr_w*SCR_DEFAULTH)/SCR_DEFAULTW;

    bool hasbpp = true;
    if(colorbits)
        hasbpp = SDL_VideoModeOK(scr_w, scr_h, colorbits, SDL_OPENGL|flags)==colorbits;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#if SDL_VERSION_ATLEAST(1, 2, 11)
    if(vsync>=0) SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, vsync);
#endif
    static int configs[] =
    {
        0x7, /* try everything */
        0x6, 0x5, 0x3, /* try disabling one at a time */
        0x4, 0x2, 0x1, /* try disabling two at a time */
        0 /* try disabling everything */
    };
    int config = 0;
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    if(!depthbits) SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    if(!fsaa)
    {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    }
    loopi(sizeof(configs)/sizeof(configs[0]))
    {
        config = configs[i];
        if(!depthbits && config&1) continue;
        if(!stencilbits && config&2) continue;
        if(fsaa<=0 && config&4) continue;
        if(depthbits) SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, config&1 ? depthbits : 16);
        if(stencilbits)
        {
            hasstencil = config&2 ? stencilbits : 0;
            SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, hasstencil);
        }
        else hasstencil = 0;
        if(fsaa>0)
        {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, config&4 ? 1 : 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, config&4 ? fsaa : 0);
        }
        screen = SDL_SetVideoMode(scr_w, scr_h, hasbpp ? colorbits : 0, SDL_OPENGL|flags);
        if(screen) break;
    }
    if(!screen) fatal("Unable to create OpenGL screen: %s", SDL_GetError());
    else
    {
        if(!hasbpp) conoutf(CON_WARN, "%d bit color buffer not supported - disabling", colorbits);
        if(depthbits && (config&1)==0) conoutf(CON_WARN, "%d bit z-buffer not supported - disabling", depthbits);
        if(stencilbits && (config&2)==0) conoutf(CON_WARN, "Stencil buffer not supported - disabling");
        if(fsaa>0 && (config&4)==0) conoutf(CON_WARN, "%dx anti-aliasing not supported - disabling", fsaa);
    }

    scr_w = screen->w;
    scr_h = screen->h;

    usedcolorbits = hasbpp ? colorbits : 0;
    useddepthbits = config&1 ? depthbits : 0;
    usedfsaa = config&4 ? fsaa : 0;
}

void resetgl()
{
    clearchanges(CHANGE_GFX);

    renderbackground("resetting OpenGL");

    extern void cleanupva();
    extern void cleanupparticles();
    extern void cleanupsky();
    extern void cleanupmodels();
    extern void cleanuptextures();
    extern void cleanuplightmaps();
    extern void cleanupblendmap();
    extern void cleanshadowmap();
    extern void cleanreflections();
    extern void cleanupglare();
    extern void cleanupdepthfx();
    extern void cleanupshaders();
    extern void cleanupgl();
    recorder::cleanup();
    cleanupva();
    cleanupparticles();
    cleanupsky();
    cleanupmodels();
    cleanuptextures();
    cleanuplightmaps();
    cleanupblendmap();
    cleanshadowmap();
    cleanreflections();
    cleanupglare();
    cleanupdepthfx();
    cleanupshaders();
    cleanupgl();

    SDL_SetVideoMode(0, 0, 0, 0);

    int usedcolorbits = 0, useddepthbits = 0, usedfsaa = 0;
    setupscreen(usedcolorbits, useddepthbits, usedfsaa);
    gl_init(scr_w, scr_h, usedcolorbits, useddepthbits, usedfsaa);

    extern void reloadfonts();
    extern void reloadtextures();
    extern void reloadshaders();
    inbetweenframes = false;
    if(!reloadtexture(*notexture) ||
       !reloadtexture("data/logo.png") ||
       !reloadtexture(tempformatstring("data/%s", backgroundimage)) ||
       !reloadtexture("data/mapshot_frame.png") ||
       !reloadtexture("data/loading_frame.png") ||
       !reloadtexture("data/loading_bar.png"))
        fatal("failed to reload core texture");
    reloadfonts();
    inbetweenframes = true;
    renderbackground("initializing...");
	restoregamma();
    reloadshaders();
    reloadtextures();
    initlights();
    allchanged(true);
}

COMMAND(resetgl, "");

vector<SDL_Event> events;

void pushevent(const SDL_Event &e)
{
    events.add(e);
}

static bool filterevent(const SDL_Event &event)
{
    switch(event.type)
    {
        case SDL_MOUSEMOTION:
            #ifndef WIN32
            if(grabinput && !(screen->flags&SDL_FULLSCREEN))
            {
                if(event.motion.x == screen->w / 2 && event.motion.y == screen->h / 2)
                    return false;  // ignore any motion events generated by SDL_WarpMouse
                #ifdef __APPLE__
                if(event.motion.y == 0)
                    return false;  // let mac users drag windows via the title bar
                #endif
            }
            #endif
            break;
    }
    return true;
}

static inline bool pollevent(SDL_Event &event)
{
    while(SDL_PollEvent(&event))
    {
        if(filterevent(event)) return true;
    }
    return false;
}

bool interceptkey(int sym)
{
    static int lastintercept = SDLK_UNKNOWN;
    int len = lastintercept == sym ? events.length() : 0;
    SDL_Event event;
    while(pollevent(event))
    {
        switch(event.type)
        {
            case SDL_MOUSEMOTION: break;
            default: pushevent(event); break;
        }
    }
    lastintercept = sym;
    if(sym != SDLK_UNKNOWN) for(int i = len; i < events.length(); i++)
    {
        if(events[i].type == SDL_KEYDOWN && events[i].key.keysym.sym == sym) { events.remove(i); return true; }
    }
    return false;
}

static void ignoremousemotion()
{
    SDL_Event e;
    SDL_PumpEvents();
    while(SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENTMASK(SDL_MOUSEMOTION)));
}

static void resetmousemotion()
{
#ifndef WIN32
    if(grabinput && !(screen->flags&SDL_FULLSCREEN))
    {
        SDL_WarpMouse(screen->w / 2, screen->h / 2);
    }
#endif
}

static void checkmousemotion(int &dx, int &dy)
{
    loopv(events)
    {
        SDL_Event &event = events[i];
        if(event.type != SDL_MOUSEMOTION)
        {
            if(i > 0) events.remove(0, i);
            return;
        }
        dx += event.motion.xrel;
        dy += event.motion.yrel;
    }
    events.setsize(0);
    SDL_Event event;
    while(pollevent(event))
    {
        if(event.type != SDL_MOUSEMOTION)
        {
            events.add(event);
            return;
        }
        dx += event.motion.xrel;
        dy += event.motion.yrel;
    }
}

void checkinput()
{
    SDL_Event event;
    int lasttype = 0, lastbut = 0;
    bool mousemoved = false;
    while(events.length() || pollevent(event))
    {
        if(events.length()) event = events.remove(0);

        switch(event.type)
        {
            case SDL_QUIT:
                quit(false);
                return;

            #if !defined(WIN32) && !defined(__APPLE__)
            case SDL_VIDEORESIZE:
                screenres(&event.resize.w, &event.resize.h);
                break;
            #endif

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                keypress(event.key.keysym.sym, event.key.state==SDL_PRESSED, uni2cube(event.key.keysym.unicode));
                break;

            case SDL_ACTIVEEVENT:
                if(event.active.state & SDL_APPINPUTFOCUS)
                    inputgrab(grabinput = event.active.gain!=0);
                if(event.active.state & SDL_APPACTIVE)
                    minimized = !event.active.gain;
                break;

            case SDL_MOUSEMOTION:
                if(grabinput)
                {
                    int dx = event.motion.xrel, dy = event.motion.yrel;
                    checkmousemotion(dx, dy);
                    if(!g3d_movecursor(dx, dy)) mousemove(dx, dy);
                    mousemoved = true;
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if(lasttype==event.type && lastbut==event.button.button) break; // why?? get event twice without it
                keypress(-event.button.button, event.button.state!=0, 0);
                lasttype = event.type;
                lastbut = event.button.button;
                break;
        }
    }
    if(mousemoved) resetmousemotion();
}

void swapbuffers(bool overlay)
{
    recorder::capture(overlay);
    SDL_GL_SwapBuffers();
}

VAR(menufps, 0, 60, 1000);
VARP(maxfps, 0, 200, 1000);

void limitfps(int &millis, int curmillis)
{
    int limit = (mainmenu || minimized) && menufps ? (maxfps ? min(maxfps, menufps) : menufps) : maxfps;
    if(!limit) return;
    static int fpserror = 0;
    int delay = 1000/limit - (millis-curmillis);
    if(delay < 0) fpserror = 0;
    else
    {
        fpserror += 1000%limit;
        if(fpserror >= limit)
        {
            ++delay;
            fpserror -= limit;
        }
        if(delay > 0)
        {
            SDL_Delay(delay);
            millis += delay;
        }
    }
}

ullong tick()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000ULL + t.tv_nsec;
}

#if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
void stackdumper(unsigned int type, EXCEPTION_POINTERS *ep)
{
    if(!ep) fatal("unknown type");
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *context = ep->ContextRecord;
    string out, t;
    formatstring(out)("Cube 2: Sauerbraten Win32 Exception: 0x%x [0x%x]\n\n", er->ExceptionCode, er->ExceptionCode==EXCEPTION_ACCESS_VIOLATION ? er->ExceptionInformation[1] : -1);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
#ifdef _AMD64_
	STACKFRAME64 sf = {{context->Rip, 0, AddrModeFlat}, {}, {context->Rbp, 0, AddrModeFlat}, {context->Rsp, 0, AddrModeFlat}, 0};
    while(::StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &sf, context, NULL, ::SymFunctionTableAccess, ::SymGetModuleBase, NULL))
	{
		union { IMAGEHLP_SYMBOL64 sym; char symext[sizeof(IMAGEHLP_SYMBOL64) + sizeof(string)]; };
		sym.SizeOfStruct = sizeof(sym);
		sym.MaxNameLength = sizeof(symext) - sizeof(sym);
		IMAGEHLP_LINE64 line;
		line.SizeOfStruct = sizeof(line);
        DWORD64 symoff;
		DWORD lineoff;
        if(SymGetSymFromAddr64(GetCurrentProcess(), sf.AddrPC.Offset, &symoff, &sym) && SymGetLineFromAddr64(GetCurrentProcess(), sf.AddrPC.Offset, &lineoff, &line))
#else
    STACKFRAME sf = {{context->Eip, 0, AddrModeFlat}, {}, {context->Ebp, 0, AddrModeFlat}, {context->Esp, 0, AddrModeFlat}, 0};
    while(::StackWalk(IMAGE_FILE_MACHINE_I386, GetCurrentProcess(), GetCurrentThread(), &sf, context, NULL, ::SymFunctionTableAccess, ::SymGetModuleBase, NULL))
	{
		union { IMAGEHLP_SYMBOL sym; char symext[sizeof(IMAGEHLP_SYMBOL) + sizeof(string)]; };
		sym.SizeOfStruct = sizeof(sym);
		sym.MaxNameLength = sizeof(symext) - sizeof(sym);
		IMAGEHLP_LINE line;
		line.SizeOfStruct = sizeof(line);
        DWORD symoff, lineoff;
        if(SymGetSymFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &symoff, &sym) && SymGetLineFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &lineoff, &line))
#endif
        {
            char *del = strrchr(line.FileName, '\\');
            formatstring(t)("%s - %s [%d]\n", sym.Name, del ? del + 1 : line.FileName, line.LineNumber);
            concatstring(out, t);
        }
    }
    fatal(out);
}
#endif

#define MAXFPSHISTORY 60

int fpspos = 0;
ullong fpshistory[MAXFPSHISTORY];

void resetfpshistory()
{
    loopi(MAXFPSHISTORY) fpshistory[i] = 1;
    fpspos = 0;
}

void updatefpshistory(ullong nanos)
{
    fpshistory[fpspos++] = max(1, min((int)1000000000ULL, (int)nanos));
    if(fpspos>=MAXFPSHISTORY) fpspos = 0;
}

void getframenanos(ullong &avg, ullong &bestdiff, ullong &worstdiff)
{
    ullong total = fpshistory[MAXFPSHISTORY-1], best = total, worst = total;
    loopi(MAXFPSHISTORY-1)
    {
        ullong nanos = fpshistory[i];
        total += nanos;
        if(nanos < best) best = nanos;
        if(nanos > worst) worst = nanos;
    }

    avg = total/MAXFPSHISTORY;
    best = best - avg;
    worstdiff = avg - worst;
}

void getfps(float &fps, float &bestdiff, float &worstdiff)
{
    ullong total = fpshistory[MAXFPSHISTORY-1], best = total, worst = total;
    loopi(MAXFPSHISTORY-1)
    {
        ullong nanos = fpshistory[i];
        total += nanos;
        if(nanos < best) best = nanos;
        if(nanos > worst) worst = nanos;
    }

    fps = (float)(1000000000.0f*MAXFPSHISTORY/total);
    bestdiff = (float)(1000000000.0f/best)-fps;
    worstdiff = fps-(float)(1000000000.0f/worst);
}

void getfps_(int *raw)
{
    if(*raw) floatret((float)(1000000000.0f/fpshistory[(fpspos+MAXFPSHISTORY-1)%MAXFPSHISTORY]));
    else
    {
        float fps, bestdiff, worstdiff;
        getfps(fps, bestdiff, worstdiff);
        intret(fps);
    }
}

QCOMMANDN(getfps, "returns the current frames-per-second-rate", "raw", getfps_, "i");

// get input lag, similar to getfps
#define MAXLAGHISTORY 60
int lagpos = 0;
ullong laghistory[MAXLAGHISTORY];

void resetlaghistory()
{
    loopi(MAXLAGHISTORY) laghistory[i] = 1;
    lagpos = 0;
}

void updatelaghistory(ullong nanos)
{
    laghistory[lagpos++] = max(1, min((int)1000000000ULL, (int)nanos));
    if(lagpos>=MAXLAGHISTORY) lagpos = 0;
}

void getlagnanos(ullong &avg, ullong &bestdiff, ullong &worstdiff)
{
    ullong total = laghistory[MAXLAGHISTORY-1], best = total, worst = total;
    loopi(MAXLAGHISTORY-1)
    {
        ullong nanos = laghistory[i];
        total += nanos;
        if(nanos < best) best = nanos;
        if(nanos > worst) worst = nanos;
    }

    avg = total/MAXLAGHISTORY;
    best = best - avg;
    worstdiff = avg - worst;
}

void getlag(float &lag, float &bestdiff, float &worstdiff)
{
    ullong total = laghistory[MAXLAGHISTORY-1], best = total, worst = total;
    loopi(MAXLAGHISTORY-1)
    {
        ullong nanos = laghistory[i];
        total += nanos;
        if(nanos < best) best = nanos;
        if(nanos > worst) worst = nanos;
    }

    lag = (float)(1000000000ULL*MAXLAGHISTORY/max((int)total, 1));
    bestdiff = (float)(1000000000ULL/max((int)best, 1)-lag);
    worstdiff = (float)(lag-1000000000ULL/max((int)worst, 1));
}

bool inbetweenframes = false, renderedframe = true;

static bool findarg(int argc, char **argv, const char *str)
{
    for(int i = 1; i<argc; i++) if(strstr(argv[i], str)==argv[i]) return true;
    return false;
}

static int clockrealbase = 0, clockvirtbase = 0;
static void clockreset() { clockrealbase = SDL_GetTicks(); clockvirtbase = totalmillis; }
VARFP(clockerror, 990000, 1000000, 1010000, clockreset());
VARFP(clockfix, 0, 0, 1, clockreset());

int getclockmillis()
{
    int millis = SDL_GetTicks() - clockrealbase;
    if(clockfix) millis = int(millis*(double(clockerror)/1000000));
    millis += clockvirtbase;
    return max(millis, totalmillis);
}

VAR(numcpus, 1, 1, 16);

// int displayrefreshrate = 0;
ullong lastdraw = 0;
ullong myframenanos = 0;

VARP(displayrefreshrate, 1, 60, 288);
ullong refreshinterval()
{
    // only possible with SDL2:
    /*SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(screen), &current);
    displayrefreshrate = current.refresh_rate;*/
    return 1000000000ULL/displayrefreshrate; // Set this individually..
}

VARP(framenanofix, -10000, 500, 10000);
bool allow_draw()
{
    ullong __usenanos = myframenanos + framenanofix;
    if((tick()-lastdraw) < (refreshinterval()-__usenanos)) return false;
    return true;
}

ullong input_nanos = 0;

#ifdef QUED32
GeoIP *geoip = NULL;
#endif

const char *serverextinfogui =
    "newgui serverextinfo [\n"
        "guiserverextinfo\n"
    "] 0\n";

VARP(benchmarkframe, 0, 0, 1);
static double totaltime = 0.0;
static uint totalcounter = 0;
void framebenchmark(bool create = false)
{
    static benchmark bench("frame", true);
    if(create && !bench.active())
        bench.start();
    else
    {
        if(bench.active())
        {
            bench.calc();
            double totaltimeold = totaltime;
            totaltime += bench.gettime();
            if(totaltime < totaltimeold)
            {
                totaltime = 0.0;
                totalcounter = 0;
            }
            else
            {
                totalcounter++;
                if(benchmarkframe)
                    conoutf("frame took %.6lf ms | median: %.6lf ms | possible fps: %.0f",
                        bench.gettime(), totaltime/double(totalcounter ? totalcounter : 1), 1000.0/bench.gettime());
                myframenanos = (ullong)(totaltime/double(totalcounter ? totalcounter : 1));
            }
            bench.reset();
        }
    }
}
ICOMMAND(resetframebenchmark, "", (), { totaltime = 0.0; totalcounter = 0.0; });

ullong elapsednanos, totalnanos;
void draw_frame()
{
    framebenchmark(true);
    lastdraw = tick();

    static int frames = 0;

    int millis = getclockmillis();
    ullong nanos = tick();
    if(!vsync) limitfps(millis, totalmillis);
    elapsedtime = millis - totalmillis;
    elapsednanos = nanos - totalnanos;

    static int timeerr = 0;
    int scaledtime = game::scaletime(elapsedtime) + timeerr;
    curtime = scaledtime/100;
    timeerr = scaledtime%100;
    if(!multiplayer(false) && curtime>200) curtime = 200;
    if(game::ispaused()) curtime = 0;
    lastmillis += curtime;
    totalmillis = millis;
    if(totalmillis >= INT_MAX-64)
    {
        totalmillis = 1;
        totalmillisrepeats++;
    }
    totalnanos = nanos;
    updatetime();

    if(vsync < 2) checkinput();
    menuprocess();
    tryedit();

    if(lastmillis) game::updateworld();
    extinfo::update();
    game::dotime();
    if(mainmenugamma) maingamma();
    isloading = false;

    checksleep(lastmillis);

    serverslice(false, 0);

    if(frames) updatefpshistory(elapsednanos);
    frames++;

    // miscellaneous general game effects
    recomputecamera();
    updateparticles();
    updatesounds();

    if(minimized && !recorder::isrecording())
    {
        framebenchmark();
        return;
    }
    inbetweenframes = false;
    if(mainmenu) gl_drawmainmenu(screen->w, screen->h);
    else gl_drawframe(screen->w, screen->h);
    swapbuffers();
    renderedframe = inbetweenframes = true;
    framebenchmark();
}

void installcommands()
{
    execute("createine = [ if (strcmp \"\" (getalias $arg1)) [ alias $arg1 $arg2 ] ]");
    execute("append = [	$arg1 = (concat (getalias $arg1) $arg2) ]");
}

extern string cursor_;
namespace game { extern const char **getgamescripts(); }
bool justquit = false;
ICOMMAND(justquittheclient, "", (), justquit = true);
int main(int argc, char **argv)
{
    #ifdef WIN32
    //atexit((void (__cdecl *)(void))_CrtDumpMemoryLeaks);
    #ifndef _DEBUG
    #ifndef __GNUC__
    __try {
    #endif
    #endif
    #endif

    setlogfile(NULL);

    int dedicated = 0;
    char *load = NULL, *initscript = NULL;

    initing = INIT_RESET;
    for(int i = 1; i<argc; i++)
    {
        if(argv[i][0]=='-') switch(argv[i][1])
        {
            case 'q':
			{
				const char *dir = sethomedir(&argv[i][2]);
				if(dir) logoutf("Using home directory: %s", dir);
				break;
			}
        }
    }
    execfile("init.cfg", false);
    for(int i = 1; i<argc; i++)
    {
        if(argv[i][0]=='-') switch(argv[i][1])
        {
            case 'q': /* parsed first */ break;
            case 'r': /* compat, ignore */ break;
            case 'k':
            {
                const char *dir = addpackagedir(&argv[i][2]);
                if(dir) logoutf("Adding package directory: %s", dir);
                break;
            }
            case 'g': logoutf("Setting log file: %s", &argv[i][2]); setlogfile(&argv[i][2]); break;
            case 'd': dedicated = atoi(&argv[i][2]); if(dedicated<=0) dedicated = 2; break;
            case 'w': scr_w = clamp(atoi(&argv[i][2]), SCR_MINW, SCR_MAXW); if(!findarg(argc, argv, "-h")) scr_h = -1; break;
            case 'h': scr_h = clamp(atoi(&argv[i][2]), SCR_MINH, SCR_MAXH); if(!findarg(argc, argv, "-w")) scr_w = -1; break;
            case 'z': depthbits = atoi(&argv[i][2]); break;
            case 'b': colorbits = atoi(&argv[i][2]); break;
            case 'a': fsaa = atoi(&argv[i][2]); break;
            case 'v': vsync = atoi(&argv[i][2]); break;
            case 't': fullscreen = atoi(&argv[i][2]); break;
            case 's': stencilbits = atoi(&argv[i][2]); break;
            case 'f':
            {
                extern int useshaders, shaderprecision, forceglsl;
                int sh = -1, prec = shaderprecision;
                for(int j = 2; argv[i][j]; j++) switch(argv[i][j])
                {
                    case 'a': case 'A': forceglsl = 0; sh = 1; break;
                    case 'g': case 'G': forceglsl = 1; sh = 1; break;
                    case 'f': case 'F': case '0': sh = 0; break;
                    case '1': case '2': case '3': if(sh < 0) sh = 1; prec = argv[i][j] - '1'; break;
                    default: break;
                }
                useshaders = sh > 0 ? 1 : 0;
                shaderprecision = prec;
                break;
            }
            case 'l':
            {
                char pkgdir[] = "packages/";
                load = strstr(path(&argv[i][2]), path(pkgdir));
                if(load) load += sizeof(pkgdir)-1;
                else load = &argv[i][2];
                break;
            }
            case 'x': initscript = &argv[i][2]; break;
            default: if(!serveroption(argv[i])) gameargs.add(argv[i]); break;
        }
        else gameargs.add(argv[i]);
    }
    initing = NOT_INITING;

    numcpus = clamp(guessnumcpus(), 1, 16);

    if(dedicated <= 1)
    {
        logoutf("init: sdl");

        int par = 0;
        #ifdef _DEBUG
        par = SDL_INIT_NOPARACHUTE;
        #ifdef WIN32
        SetEnvironmentVariable("SDL_DEBUG", "1");
        #endif
        #endif

        if(SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO|SDL_INIT_AUDIO|par)<0) fatal("Unable to initialize SDL: %s", SDL_GetError());

        const SDL_version *v = SDL_Linked_Version();
        conoutf(CON_INIT, "Library: SDL %u.%u.%u", v->major, v->minor, v->patch);
    }

    logoutf("init: net");
    if(enet_initialize()<0) fatal("Unable to initialise network module");
    atexit(enet_deinitialize);
    enet_time_set(0);

    logoutf("init: game");
    game::parseoptions(gameargs);
    initserver(dedicated>0, dedicated>1);  // never returns if dedicated
    ASSERT(dedicated <= 1);
    game::initclient();

    logoutf("init: video");
    const SDL_VideoInfo *video = SDL_GetVideoInfo();
    if(video)
    {
        desktopw = video->current_w;
        desktoph = video->current_h;
    }
    int usedcolorbits = 0, useddepthbits = 0, usedfsaa = 0;
    setupscreen(usedcolorbits, useddepthbits, usedfsaa);

    SDL_WM_SetCaption("Quality's Sauerbraten", NULL);
    keyrepeat(false);
    SDL_ShowCursor(0);

    logoutf("init: gl");
    gl_checkextensions();
    gl_init(scr_w, scr_h, usedcolorbits, useddepthbits, usedfsaa);
    notexture = textureload("packages/textures/notexture.png");
    if(!notexture) fatal("could not find core textures");

    logoutf("init: console");
    if(!execfile("data/stdlib.cfg", false)) fatal("cannot find data files (you are running from the wrong folder, try .bat file in the main folder)");   // this is the first file we load.
    if(!execfile("data/font.cfg", false)) fatal("cannot find font definitions");
    if(!setfont("default")) fatal("no default font specified");

    inbetweenframes = true;
    renderbackground("initializing...");

    logoutf("init: effects");
    loadshaders();
    particleinit();
    initdecals();

    logoutf("init: world");
    camera1 = player = game::iterdynents(0);
    emptymap(0, true, NULL, false);

    logoutf("init: sound");
    initsound();

    logoutf("init: cfg");
    execfile("data/keymap.cfg");
    execfile("data/stdedit.cfg");
    execfile("data/menus.cfg");
    execfile("data/sounds.cfg");
    execfile("data/heightmap.cfg");
    execfile("data/blendbrush.cfg");
    if(game::savedservers()) execfile(game::savedservers(), false);

    identflags |= IDF_PERSIST;

    initing = INIT_LOAD;
    if(!execfile(game::savedconfig(), false))
    {
        execfile(game::defaultconfig());
        writecfg(game::restoreconfig());
    }
    execfile(game::autoexec(), false);
    if(game::getgamescripts())
    {
        const char **gamescripts = game::getgamescripts();
        if(gamescripts)
        {
            logoutf("init: extended game scripts");
            identflags &= ~IDF_PERSIST;
            for(int i = 0; gamescripts[i] != 0; i++)
                executestr(gamescripts[i]);
            identflags |= IDF_PERSIST;
        }
    }
    initing = NOT_INITING;

    identflags &= ~IDF_PERSIST;

    string gamecfgname;
    copystring(gamecfgname, "data/game_");
    concatstring(gamecfgname, game::gameident());
    concatstring(gamecfgname, ".cfg");
    execfile(gamecfgname);

    game::loadconfigs();

    identflags |= IDF_PERSIST;

    if(execfile("once.cfg", false)) remove(findfile("once.cfg", "rb"));

    if(load)
    {
        logoutf("init: localconnect");
        //localconnect();
        game::changemap(load);
    }

    if(initscript) execute(initscript);

    logoutf("init: QuEd");
    loadhistory();
    execfile("favservs.cfg", false);
    execfile("clantags.cfg", false);
    execfile("data/edut.cfg", false);
    execfile("data/init", false);
    execfile("wheelzoom.cfg", false);
    executestr(serverextinfogui);
    installcommands();
    ipignore::startup();
    game::loadstats();
    whois::loadwhoisdb();

    #ifdef QUED32
    logoutf("init: GeoIP");
    geoip = GeoIP_open(findfile("data/GeoIP.dat", "r"), 0);
    if(!geoip) conoutf("\f3error: could not locate GeoIP.dat\n\tGeoIP disabled!");
    #endif

    logoutf("init: mainloop");

    initmumble();
    resetfpshistory();

    inputgrab(grabinput = true);
    ignoremousemotion();

    conoutf("\f2Quality \f1Edition \f3%s\f7, \f0%s", getfullversionname(), is64bitpointer() ? "64bit" : "32bit");
    #ifndef WIN32
    conoutf("\f3Be careful: This client is only tested on windows.. \n\tI will take no accuses for anything that might break on your PC! (e.g. system cmds..)");
    #endif // WIN32
    event::run(event::STARTUP);
    copystring(cursor_, "data/guicursor.png");

    for(;;)
    {
        if(justquit) break;
        switch(vsync)
        {
            case 2:
            {
                checkinput();
                if(allow_draw()) draw_frame();
                updatelaghistory((int)(tick()-input_nanos));
                input_nanos = tick();
                break;
            }
            case 1:
            {
                if(!allow_draw()) break;
                draw_frame();
                updatelaghistory((int)(tick()-input_nanos));
                input_nanos = tick();
                break;
            }
            default:
            {
                draw_frame();
                updatelaghistory((int)(tick()-input_nanos));
                input_nanos = tick();
                break;
            }
        }
    }

    if(justquit) return 0;

    ASSERT(0);
    return EXIT_FAILURE;

    #if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
    } __except(stackdumper(0, GetExceptionInformation()), EXCEPTION_CONTINUE_SEARCH) { return 0; }
    #endif
}

