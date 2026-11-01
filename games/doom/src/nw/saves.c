/* Doom on the calculator: saved games.
 *
 * Doom's six slots are files, doomsav0.dsg to doomsav5.dsg, in RP2040 Doom's compressed format (a few KB
 * each). Home and On/Off keep the moment in doomres.sav, loaded once on the next start. The settings
 * (messages, brightness) are in doom.cfg. Installing apps from the NumWorks website keeps only Python
 * scripts, so all of these are copied into doom_saves.py too, one comment line "#>name:base64" each (the
 * way NumPlay and its big games keep theirs), and come back from it when the files are gone. */
#include <string.h>

#include "doomtype.h"
#include "doom/doomstat.h"
#include "doom/g_game.h"
#include "doom/m_menu.h"
#include "doom/p_saveg.h"
#include "i_video.h"
#include "plat.h"
#include "nw.h"

#define COPY_NAME "doom_saves.py"
#define CFG_NAME "doom.cfg"

extern isb_int8_t showMessages;

static const char *const kept[NW_SLOTS + 1] = {
    "doomsav0.dsg", "doomsav1.dsg", "doomsav2.dsg", "doomsav3.dsg", "doomsav4.dsg", "doomsav5.dsg",
    "doomres.sav", CFG_NAME
};
#define NKEPT (int)(sizeof kept / sizeof kept[0])
static const char copy_head[] = "# Doom's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void copy_write(void) {
    uint32_t total = 1 + (sizeof copy_head - 1) + 1, len;
    int n = 0;
    for (int k = 0; k < NKEPT; k++)
        if (plat_load(kept[k], &len) && len) total += 2 + (uint32_t)strlen(kept[k]) + 1 + (len + 2) / 3 * 4 + 1, n++;
    plat_remove(COPY_NAME);
    if (!n) return;
    uint8_t *o = plat_reserve(COPY_NAME, total), *p = o;
    if (!o) return;
    *p++ = 0; /* (the script's status byte: not imported) */
    memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
    for (int k = 0; k < NKEPT; k++) {
        const uint8_t *d = plat_load(kept[k], &len);
        if (!d || !len) continue;
        uint32_t nl = (uint32_t)strlen(kept[k]);
        *p++ = '#', *p++ = '>';
        memcpy(p, kept[k], nl), p += nl;
        *p++ = ':';
        for (uint32_t i = 0; i < len; i += 3, p += 4) {
            /* (written out: GCC 15.2 for the calculator got a loop over the four wrong, see NumPlay's
             * launcher/src/storage.c) */
            uint32_t left = len - i, v = (uint32_t)d[i] << 16 | (left > 1 ? d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
            p[0] = (uint8_t)b64[v >> 18 & 63];
            p[1] = (uint8_t)b64[v >> 12 & 63];
            p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
            p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
        }
        *p++ = '\n';
    }
    *p++ = 0;
    plat_reserved(COPY_NAME, (uint32_t)(p - o));
}

static int b64_value(uint8_t c) {
    for (int i = 0; i < 64; i++)
        if ((uint8_t)b64[i] == c) return i;
    return -1;
}

static const uint8_t *copy_line(int k, uint32_t *len) {
    uint32_t n, nl = (uint32_t)strlen(kept[k]);
    const uint8_t *c = plat_load(COPY_NAME, &n);
    if (!c) return NULL;
    for (uint32_t i = 0; i + nl + 3 <= n; i++)
        if (c[i] == '#' && c[i + 1] == '>' && !memcmp(c + i + 2, kept[k], nl) && c[i + 2 + nl] == ':') {
            const uint8_t *l = c + i + 3 + nl;
            uint32_t m = 0;
            while (l + m < c + n && l[m] != '\n' && l[m]) m++;
            *len = m;
            return l;
        }
    return NULL;
}

void nw_saves_restore(void) {
    for (int k = 0; k < NKEPT; k++) {
        uint32_t have, len;
        if (plat_load(kept[k], &have)) continue;
        const uint8_t *l = copy_line(k, &len);
        if (!l || !len || len % 4) continue;
        uint32_t size = len / 4 * 3 - (l[len - 1] == '=') - (l[len - 2] == '=');
        uint8_t *out = plat_reserve(kept[k], size);
        if (!out) continue;
        l = copy_line(k, &len);
        boolean ok = l != NULL && len / 4 * 3 - (l[len - 1] == '=') - (l[len - 2] == '=') == size;
        uint32_t o = 0;
        for (uint32_t i = 0; ok && i < len; i += 4) {
            int v0 = l[i] == '=' ? 0 : b64_value(l[i]), v1 = l[i + 1] == '=' ? 0 : b64_value(l[i + 1]);
            int v2 = l[i + 2] == '=' ? 0 : b64_value(l[i + 2]), v3 = l[i + 3] == '=' ? 0 : b64_value(l[i + 3]);
            if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) ok = false;
            uint32_t wd = (uint32_t)(v0 << 18 | v1 << 12 | v2 << 6 | v3);
            if (o < size) out[o++] = (uint8_t)(wd >> 16);
            if (o < size) out[o++] = (uint8_t)(wd >> 8);
            if (o < size) out[o++] = (uint8_t)wd;
        }
        if (ok && o == size) plat_reserved(kept[k], size);
        else plat_remove(kept[k]);
    }
}

/* ------------------------------------------------------------------ the slots, for g_game.c and m_menu.c */
void P_SaveGameGetExistingFlashSlotAddresses(flash_slot_info_t *slots, int count) {
    for (int i = 0; i < count; i++) {
        uint32_t len = 0;
        const uint8_t *d = i < NW_SLOTS ? plat_load(kept[i], &len) : NULL;
        slots[i].data = d && len > SAVESTRINGSIZE ? d : NULL;
        slots[i].size = slots[i].data ? (int)len : 0;
    }
}

boolean P_SaveGameWriteFlashSlot(int slot, const uint8_t *buffer, uint size, uint8_t *buffer4k) {
    if (slot < 0 || slot >= NW_SLOTS) return false;
    if (!buffer) {
        plat_remove(kept[slot]);
        copy_write();
        return true;
    }
    if (!plat_save(kept[slot], buffer, size)) return false;
    copy_write();
    return true;
}

/* ------------------------------------------------------------------ the moment, on Home */
boolean nw_resume_ready(void) {
    uint32_t len;
    return plat_load(kept[NW_RESUME_SLOT], &len) && len > SAVESTRINGSIZE;
}

void G_SaveGameNow(int slot, const char *description);

void nw_resume_save(void) {
    if (gamestate == GS_LEVEL && usergame && !demoplayback && players[consoleplayer].playerstate == PST_LIVE) {
        G_SaveGameNow(NW_RESUME_SLOT, "the moment");
    } else if (plat_remove(kept[NW_RESUME_SLOT])) {
        copy_write();
    }
}

void nw_resume_done(void) {
    if (plat_remove(kept[NW_RESUME_SLOT])) copy_write();
}

/* ------------------------------------------------------------------ settings */
boolean nw_first_start;

/* 'D', the version (3), messages, brightness, the map over the view, secrets on the map, the screen's refresh
 * (left to right, right to left, none), the detail, the screen size, always run, then each calculator key's
 * action (NK_COUNT of them). Version 2 had no always run. */
#define CFG_LEN (10 + NK_COUNT)
extern int nw_map_overlay, nw_map_secrets, nw_scan_right_to_left, nw_sync_off;
extern isb_int8_t detailLevel, screenblocks;

void nw_settings_load(void) {
    uint32_t len;
    const uint8_t *d = plat_load(CFG_NAME, &len);
    nw_first_start = !d;
    if (!d || len < 4 || d[0] != 'D' || d[1] < 1 || d[1] > 3) return;
    showMessages = d[2] ? 1 : 0;
    usegamma = d[3] <= 4 ? d[3] : 0;
    if (d[1] == 1 || len != CFG_LEN - (d[1] == 2)) return;
    nw_map_overlay = d[4] != 0;
    nw_map_secrets = d[5] != 0;
    nw_scan_right_to_left = d[6] == 1;
    nw_sync_off = d[6] == 2;
    detailLevel = d[7] == 1;
    screenblocks = d[8] >= 3 && d[8] <= 11 ? d[8] : 10;
    int at = 9;
    if (d[1] == 3) nw_always_run = d[at++] != 0;
    for (int k = 0; k < NK_COUNT; k++) nw_binding[k] = d[at + k] < NA_COUNT ? d[at + k] : NA_NONE;
    if (d[1] == 2) {   /* (the weapon keys came after: on ( and ) if those do nothing) */
        if (nw_binding[NK_LPAREN] == NA_NONE) nw_binding[NK_LPAREN] = NA_PREVWEAPON;
        if (nw_binding[NK_RPAREN] == NA_NONE) nw_binding[NK_RPAREN] = NA_NEXTWEAPON;
    }
    M_NwViewLoaded();
}

void nw_settings_save(void) {
    uint8_t d[CFG_LEN] = {'D', 3, (uint8_t)showMessages, (uint8_t)usegamma, (uint8_t)nw_map_overlay,
                          (uint8_t)nw_map_secrets, (uint8_t)(nw_sync_off ? 2 : nw_scan_right_to_left),
                          (uint8_t)detailLevel, (uint8_t)screenblocks, (uint8_t)nw_always_run};
    memcpy(d + 10, nw_binding, NK_COUNT);
    uint32_t len;
    const uint8_t *old = plat_load(CFG_NAME, &len);
    if (old && len == sizeof d && !memcmp(old, d, sizeof d)) return;
    if (plat_save(CFG_NAME, d, sizeof d)) copy_write();
}
