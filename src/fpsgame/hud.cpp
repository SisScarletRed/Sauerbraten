#include "game.h"

using namespace game;
extern void drawfragmsg(fpsent *d, int w, int h),
            drawacoloredquad(float x, float y, float w, float h,
                             uchar r, uchar g, uchar b, uchar a);
extern bool guiisshowing();
extern int getpacketloss(),
           fullconsole;
namespace game
{
    extern double getweaponaccuracy(int gun, fpsent *f = NULL);
    extern bool isduel(bool allowspec = false, int colors = 0);
}
VARP(newhud, 0, 1, 1);
float staticscale = 0.33f;
FVARFP(hudscale, 50, 100, 300, { staticscale = 0.33f*(hudscale/100.0f); });

namespace hud
{
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

    void drawnotices()
    {
        glPushMatrix();
        glScalef(noticescale, noticescale, 1);
        int ty = int(((hudheight/2)+(hudheight/2*noticeoffset))/noticescale), tx = int((hudwidth/2)/noticescale),
            tf = int(255*hudblend*noticeblend), tr = 255, tg = 255, tb = 255,
            tw = int((hudwidth-((hudsize*edgesize)*2+(hudsize*inventoryleft)+(hudsize*inventoryright)))/noticescale);
        if(noticestone) skewcolour(tr, tg, tb, noticestone);

        if(!gs_playing(game::gamestate) || lastmillis-game::maptime <= noticetitle)
        {
            ty += draw_textx("%s", tx, ty, 255, 255, 255, tf, TEXT_CENTERED, -1, tw, *maptitle ? maptitle : mapname);
            pushfont("reduced");
            if(*mapauthor) ty += draw_textx("by %s", tx, ty, 255, 255, 255, tf, TEXT_CENTERED, -1, tw, mapauthor);
            defformatstring(gname)("%s", server::gamename(game::gamemode, game::mutators, 0, 32));
            ty += draw_textx("[ \fs\fa%s\fS ]", tx, ty, 255, 255, 255, tf, TEXT_CENTERED, -1, tw, gname);
            popfont();
            ty += FONTH/3;
        }
        if(game::gamestate == G_S_INTERMISSION)
            ty += draw_textx("Intermission", tx, ty, 255, 255, 255, tf, TEXT_CENTERED, -1, tw)+FONTH/3;
        else if(game::gamestate == G_S_VOTING) ty += draw_textx("Voting in progress", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw)+FONTH/3;
        else if(client::demoplayback && showdemoplayback)
            ty += draw_textx("Demo playback in progress", tx, ty, 255, 255, 255, tf, TEXT_CENTERED, -1, tw)+FONTH/3;
        else if(game::gamestate == G_S_WAITING) ty += draw_textx("Waiting for players", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw)+FONTH/3;
        else if(hastkwarn(game::focus)) // first and foremost
            ty += draw_textx("\fzryDo NOT shoot team-mates", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, -1)+FONTH/3;
        popfont();

        pushfont("default");
        if(game::player1->quarantine)
        {
            ty += draw_textx("You are \fzoyQUARANTINED", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw);
            ty += draw_textx("Please await instructions from a moderator", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw)+FONTH/3;
        }
        else if(game::player1->state == CS_SPECTATOR)
            ty += draw_textx("[ %s ]", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, specviewname())+FONTH/3;
        else if(game::player1->state == CS_WAITING && showname())
            ty += draw_textx("[ %s ]", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, game::colourname(game::focus))+FONTH/3;

        if(gs_playing(game::gamestate))
        {
            gameent *target = game::player1->state != CS_SPECTATOR ? game::player1 : game::focus;
            if(target->state == CS_DEAD || target->state == CS_WAITING)
            {
                int delay = target->respawnwait(lastmillis, m_delay(game::gamemode, game::mutators, target->team));
                SEARCHBINDCACHE(attackkey)("primary", 0);
                if(delay || m_duke(game::gamemode, game::mutators) || (m_fight(game::gamemode) && maxalive > 0))
                {
                    if(game::gamestate == G_S_WAITING || m_duke(game::gamemode, game::mutators)) ty += draw_textx("Queued for new round", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw);
                    else if(delay) ty += draw_textx("%s: Down for \fs\fy%s\fS", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, target == game::player1 && target->state == CS_WAITING ? "Please Wait" : "Fragged", timestr(delay));
                    else if(target == game::player1 && target->state == CS_WAITING && m_fight(game::gamemode) && maxalive > 0 && maxalivequeue)
                    {
                        int n = game::numwaiting(), x = max(int(G(maxalive)*G(maxplayers)), max(int(client::otherclients(true, true)*G(maxalivethreshold)), G(maxaliveminimum)));
                        if(m_team(game::gamemode, game::mutators))
                        {
                            if(x%2) x++;
                            x = x/2;
                        }
                        ty += draw_textx("Maximum arena capacity is: \fs\fg%d\fS %s", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, x, x != 1 ? "players" : "player");
                        pushfont("reduced");
                        if(n) ty += draw_textx("Respawn queued, waiting for \fs\fy%d\fS %s", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, n, n != 1 ? "players" : "player");
                        else ty += draw_textx("Prepare to respawn, you are \fs\fzygnext\fS in the queue", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw);
                        popfont();
                    }
                    if(target == game::player1 && target->state != CS_WAITING && shownotices >= 2 && lastmillis-target->lastdeath >= 500)
                    {
                        pushfont("little");
                        ty += draw_textx("Press %s to enter respawn queue", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, attackkey);
                        popfont();
                    }
                }
                else
                {
                    ty += draw_textx("Ready to respawn", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, -1);
                    if(target == game::player1 && target->state != CS_WAITING && shownotices >= 2)
                    {
                        pushfont("little");
                        ty += draw_textx("Press %s to respawn now", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, attackkey);
                        popfont();
                    }
                }
                if(obitnotices && target->lastdeath && (target->state == CS_WAITING || target->state == CS_DEAD) && *target->obit)
                {
                    pushfont("reduced");
                    ty += draw_textx("%s", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, target->obit);
                    popfont();
                }
                if(shownotices >= 2)
                {
                    if(target == game::player1 && !client::demoplayback)
                    {
                        if(target->state == CS_WAITING && shownotices >= 2)
                        {
                            SEARCHBINDCACHE(waitmodekey)("waitmodeswitch", 3);
                            pushfont("little");
                            ty += draw_textx("Press %s to %s", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, waitmodekey, game::tvmode() ? "interact" : "switch to TV");
                            popfont();
                        }
                        if(m_loadout(game::gamemode, game::mutators))
                        {
                            SEARCHBINDCACHE(loadkey)("showgui profile 2", 0);
                            pushfont("little");
                            ty += draw_textx("Press %s to \fs%s\fS loadout", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, loadkey, target->loadweap.empty() ? "\fzoyselect" : "change");
                            popfont();
                        }
                        if(m_fight(game::gamemode) && m_team(game::gamemode, game::mutators))
                        {
                            SEARCHBINDCACHE(teamkey)("showgui team", 0);
                            pushfont("little");
                            ty += draw_textx("Press %s to change teams", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, teamkey);
                            popfont();
                        }
                    }
                }
            }
            else if(target->state == CS_ALIVE)
            {
                if(obitnotices && totalmillis-target->lastkill <= noticetime && *target->obit)
                {
                    pushfont("reduced");
                    ty += draw_textx("%s", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, target->obit);
                    popfont();
                }
                if(target == game::player1 && shownotices >= 2 && game::allowmove(target))
                {
                    pushfont("emphasis");
                    static vector<actitem> actitems;
                    actitems.setsize(0);
                    vec pos = target->center();
                    float radius = max(target->height*0.5f, max(target->xradius, target->yradius));
                    if(entities::collateitems(target, pos, radius, actitems))
                    {
                        SEARCHBINDCACHE(actionkey)("use", 0, "\f{\fs\fzuy", "\fS}");
                        while(!actitems.empty())
                        {
                            actitem &t = actitems.last();
                            int ent = -1;
                            switch(t.type)
                            {
                                case actitem::ENT:
                                {
                                    if(!entities::ents.inrange(t.target)) break;
                                    ent = t.target;
                                    break;
                                }
                                case actitem::PROJ:
                                {
                                    if(!projs::projs.inrange(t.target)) break;
                                    projent &proj = *projs::projs[t.target];
                                    ent = proj.id;
                                    break;
                                }
                                default: break;
                            }
                            if(entities::ents.inrange(ent))
                            {
                                extentity &e = *entities::ents[ent];
                                if(enttype[e.type].usetype == EU_ITEM && e.type == WEAPON)
                                {
                                    int sweap = m_weapon(game::gamemode, game::mutators), attr = w_attr(game::gamemode, game::mutators, e.type, e.attrs[0], sweap);
                                    if(target->canuse(e.type, attr, e.attrs, sweap, lastmillis, (1<<W_S_SWITCH)|(1<<W_S_RELOAD)))
                                    {
                                        int drop = -1;
                                        if(m_classic(game::gamemode, game::mutators) && w_carry(target->weapselect, sweap) && target->ammo[attr] < 0 && w_carry(attr, sweap) && target->carry(sweap) >= maxcarry)
                                            drop = target->drop(sweap);
                                        if(isweap(drop))
                                        {
                                            static struct dropattrs : attrvector { dropattrs() { add(0, 5); } } attrs;
                                            attrs[0] = drop;
                                            defformatstring(dropweap)("%s", entities::entinfo(WEAPON, attrs, false, true));
                                            ty += draw_textx("Press %s to swap \fs%s\fS for \fs%s\fS", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, actionkey, dropweap, entities::entinfo(e.type, e.attrs, false, true));
                                        }
                                        else ty += draw_textx("Press %s to pickup \fs%s\fS", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, actionkey, entities::entinfo(e.type, e.attrs, false, true));
                                        break;
                                    }
                                }
                                else if(e.type == TRIGGER && e.attrs[2] == TA_ACTION)
                                {
                                    ty += draw_textx("Press %s to interact", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, actionkey);
                                    break;
                                }
                            }
                            actitems.pop();
                        }
                    }
                    popfont();
                    if(shownotices >= 4)
                    {
                        pushfont("little");
                        if(target->canshoot(target->weapselect, 0, m_weapon(game::gamemode, game::mutators), lastmillis, (1<<W_S_RELOAD)))
                        {
                            SEARCHBINDCACHE(attackkey)("primary", 0);
                            ty += draw_textx("Press %s to attack", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, attackkey);
                            SEARCHBINDCACHE(altkey)("secondary", 0);
                            ty += draw_textx("Press %s to %s", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, altkey, W2(target->weapselect, cooked, true)&W_C_ZOOM ? "zoom" : "alt-attack");
                        }
                        if(target->canreload(target->weapselect, m_weapon(game::gamemode, game::mutators), false, lastmillis))
                        {
                            SEARCHBINDCACHE(reloadkey)("reload", 0);
                            ty += draw_textx("Press %s to reload ammo", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, reloadkey);
                        }
                        popfont();
                    }
                }
            }

            if(game::player1->state == CS_SPECTATOR)
            {
                SEARCHBINDCACHE(speconkey)("spectator 0", 1);
                pushfont("little");
                if(!client::demoplayback)
                {
                    ty += draw_textx("Press %s to join the game", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, speconkey);
                    if(m_fight(game::gamemode) && m_team(game::gamemode, game::mutators) && shownotices >= 2)
                    {
                        SEARCHBINDCACHE(teamkey)("showgui team", 0);
                        ty += draw_textx("Press %s to join a team", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, teamkey);
                    }
                }
                if(!m_edit(game::gamemode) && shownotices >= 2)
                {
                    SEARCHBINDCACHE(specmodekey)("specmodeswitch", 1);
                    ty += draw_textx("Press %s to %s", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, specmodekey, game::tvmode() ? "interact" : "switch to TV");
                }
                popfont();
            }

            if(m_edit(game::gamemode) && (game::focus->state != CS_EDITING || shownotices >= 4) && !client::demoplayback)
            {
                SEARCHBINDCACHE(editkey)("edittoggle", 1);
                pushfont("little");
                ty += draw_textx("Press %s to %s editmode", tx, ty, tr, tg, tb, tf, TEXT_CENTERED, -1, tw, editkey, game::focus->state != CS_EDITING ? "enter" : "exit");
                popfont();
            }

            if(m_capture(game::gamemode)) capture::drawnotices(hudwidth, hudheight, tx, ty, tf/255.f);
            else if(m_defend(game::gamemode)) defend::drawnotices(hudwidth, hudheight, tx, ty, tf/255.f);
            else if(m_bomber(game::gamemode)) bomber::drawnotices(hudwidth, hudheight, tx, ty, tf/255.f);
        }
        popfont();
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
}
