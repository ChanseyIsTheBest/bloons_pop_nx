/* bp_config.c -- config.txt: the player's settings, read at every boot. MIT.
 *
 * resolution -- the game's PORTRAIT resolution, by its "p" number (the width):
 * 720 = 720 x 1280 (default) up to 1080 = 1080 x 1920. One setting for handheld
 * and docked. Only the sharpness changes; the picture is presented exactly as
 * before. Values are snapped to the nearest multiple of 18, the sizes where
 * width * 16 / 9 is a whole number, so the image keeps the exact 9:16 shape
 * and every buffer divides evenly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "bp_config.h"
#include "unity_jni.h"
#include "util.h"

const char *bp_game_root(void);

int bp_res_w = 720, bp_res_h = 1280;
int bp_portrait_rot = 1;                 /* 1 = 90 CW (right Joy-Con up), 2 = 90 CCW (left Joy-Con up) */

/* Each setting's block in the template. A config.txt written by an older build
 * that lacks one gets that block appended, so new options show up without the
 * player deleting the file. */
static const struct { const char *key; const char *block; } SECTIONS[] = {
  { "resolution",
    "# --- resolution ------------------------------------------------------\n"
    "# The game's portrait resolution, by its \"p\" number (the width):\n"
    "#    720  ->  720 x 1280   (default)\n"
    "#    810  ->  810 x 1440\n"
    "#    900  ->  900 x 1600\n"
    "#    990  ->  990 x 1760\n"
    "#   1080  -> 1080 x 1920\n"
    "# Any value from 720 to 1080 is accepted and rounded to the nearest size that\n"
    "# keeps the exact 9:16 shape. One setting for both handheld and docked.\n"
    "# Higher is sharper (most visible on a TV) but costs performance.\n"
    "resolution = 720\n" },
  { "portrait",
    "# --- portrait --------------------------------------------------------\n"
    "# Which way the picture is turned to stand upright:\n"
    "#   1 = rotate 90 degrees clockwise (right Joy-Con up, default)\n"
    "#   2 = rotate 90 degrees counter-clockwise (left Joy-Con up)\n"
    "portrait = 1\n" },
};
#define N_SECTIONS ((int)(sizeof SECTIONS / sizeof *SECTIONS))

#define RES_MIN 720
#define RES_MAX 1080

static void write_template(const char *path) {
  FILE *f = fopen(path, "w");
  if (!f) { debugPrintf("[config] could not write %s\n", path); return; }
  fputs("# config.txt -- Bloons Pop settings, read at every launch.\n", f);
  for (int i = 0; i < N_SECTIONS; i++) fprintf(f, "\n%s", SECTIONS[i].block);
  fclose(f);
  debugPrintf("[config] wrote %s (defaults)\n", path);
}

/* Does the text have a line "key =" or "#key =" (any spacing)? Prose that merely
 * contains the word does not count. */
static int mentions_key(const char *text, const char *key) {
  const size_t kl = strlen(key);
  for (const char *p = text; p && *p; ) {
    while (*p == ' ' || *p == '\t' || *p == '#') p++;
    if (!strncmp(p, key, kl)) {
      const char *q = p + kl;
      while (*q == ' ' || *q == '\t') q++;
      if (*q == '=') return 1;
    }
    p = strchr(p, '\n');
    if (p) p++;
  }
  return 0;
}
static void append_missing(const char *path) {
  char text[16384];
  FILE *f = fopen(path, "r");
  if (!f) return;
  const size_t n = fread(text, 1, sizeof text - 1, f);
  fclose(f);
  text[n] = 0;
  for (int i = 0; i < N_SECTIONS; i++) {
    if (mentions_key(text, SECTIONS[i].key)) continue;
    if (!(f = fopen(path, "a"))) return;
    fprintf(f, "%s\n%s", (n && text[n - 1] != '\n') ? "\n" : "", SECTIONS[i].block);
    fclose(f);
    debugPrintf("[config] added the new \"%s\" setting to %s\n", SECTIONS[i].key, path);
  }
}

static void trim(char *s) {
  char *p = s;
  while (*p && isspace((unsigned char)*p)) p++;
  if (p != s) memmove(s, p, strlen(p) + 1);
  size_t n = strlen(s);
  while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

void bp_config_load(void) {
  char path[640], line[256];
  snprintf(path, sizeof path, "%s/config.txt", bp_game_root());
  FILE *f = fopen(path, "r");
  if (!f) write_template(path);
  else { fclose(f); append_missing(path); }
  f = fopen(path, "r");
  int res = RES_MIN;
  while (f && fgets(line, sizeof line, f)) {
    char *hash = strchr(line, '#');
    if (hash) *hash = 0;
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char *key = line, *val = eq + 1;
    trim(key); trim(val);
    if (!*key || !*val) continue;
    if (!strcmp(key, "resolution")) {
      char *end;
      long v = strtol(val, &end, 10);
      if (end != val && (!*end || !strcmp(end, "p") || !strcmp(end, "P"))) res = (int)v;
      else debugPrintf("[config] resolution \"%s\" is not a number -- using %d\n", val, res);
    } else if (!strcmp(key, "portrait")) {
      if (!strcmp(val, "1") || !strcmp(val, "2")) bp_portrait_rot = val[0] - '0';
      else debugPrintf("[config] portrait \"%s\" must be 1 or 2 -- using %d\n", val, bp_portrait_rot);
    } else {
      debugPrintf("[config] unknown setting \"%s\" -- ignored\n", key);
    }
  }
  if (f) fclose(f);
  int s = res < RES_MIN ? RES_MIN : res > RES_MAX ? RES_MAX : res;
  s = ((s + 9) / 18) * 18;                               /* nearest exact 9:16 width */
  if (s != res) debugPrintf("[config] resolution %d -> %d (nearest exact 9:16 size from %d to %d)\n",
                            res, s, RES_MIN, RES_MAX);
  bp_res_w = s;
  bp_res_h = s * 16 / 9;
  debugPrintf("[config] resolution %dp: the game renders %d x %d (portrait)\n", s, bp_res_w, bp_res_h);
  debugPrintf("[config] portrait %d: rotated 90 degrees %s (%s Joy-Con up)\n", bp_portrait_rot,
              bp_portrait_rot == 2 ? "counter-clockwise" : "clockwise", bp_portrait_rot == 2 ? "left" : "right");
}

/* Unity saved its last screen size in PlayerPrefs (720 x 1280 in the first saves).
 * Keep it in step with config.txt so an old entry can never pull a new resolution
 * back. Only keys the game already wrote are touched. */
void bp_config_sync_prefs(void) {
  static const char *const K[2] = { "Screenmanager Resolution Width", "Screenmanager Resolution Height" };
  char v[2][16];
  snprintf(v[0], sizeof v[0], "%d", bp_res_w);
  snprintf(v[1], sizeof v[1], "%d", bp_res_h);
  int changed = 0;
  for (int i = 0; i < 2; i++) {
    const char *cur = bp_prefs_has(K[i]) ? bp_prefs_get(K[i]) : NULL;
    if (cur && strcmp(cur, v[i])) { bp_prefs_set('I', K[i], v[i]); changed = 1; }
  }
  if (changed) {
    bp_prefs_commit();
    debugPrintf("[config] Unity's saved screen size updated to %s x %s\n", v[0], v[1]);
  }
}
