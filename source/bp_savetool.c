/* bp_savetool.c -- edit Bloons Pop's own save at boot, from save.txt. MIT.
 *
 * Modelled on papapear_nx's pps_savetool: the first boot writes a save.txt with
 * every option commented out; remove the '#' in front of a line to set that
 * value. It is applied at EVERY boot, before the engine starts, for as long as
 * the line stays uncommented.
 *
 * THE FORMAT -- NinjaKiwi.Players.Files, read out of the game's own code
 * -----------------------------------------------------------------------
 *     0x00 int32   format version 1, 0x04 int32 record length (36)
 *     0x08 record  SaveCount int32, Guid[16], DateCreated int64, DateModified int64
 *     0x2c uint64  password version (2)
 *     0x34 24 B    salt
 *     0x4c ...     AES-128-CBC( zlib( UTF-8 JSON with BOM ) ), PKCS7
 * Key material: Rfc2898DeriveBytes(password, salt, 10) = PBKDF2-HMAC-SHA1,
 * first 16 bytes -> IV, next 16 -> Key (Constants.GetAes). The password comes
 * from PlayerSavePasswordGenerator(appID): version 2 is appID.ToString(), and
 * Bloons Pop's app ID is 19 (BloonsPop.PlayerService.CreateSkuSettingsHelper).
 *
 * WHY EDIT IN PLACE RATHER THAN REGENERATE (papapear_nx's reasoning)
 * -----------------------------------------------------------------
 * Values are substituted textually in the decoded JSON, leaving every other
 * byte exactly as the engine wrote it. Only TOP-LEVEL keys are touched, so a
 * "monkeyMoney" nested inside a reward entry is never mistaken for the
 * player's. A key the save does not contain is reported, never inserted.
 *
 * SAFETY: the new file is re-decoded in memory and compared before anything is
 * written; Profile.Save.orig keeps the untouched original (written once, never
 * overwritten); the save is replaced through a temp file and a rename.
 */
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>
#include "bp_savetool.h"
#include "util.h"

const char *bp_game_root(void);

#define MAX_SAVE (4u << 20)

/* PowerUpType (dump.cs). 1-15 are the power-ups the player holds and are listed
 * in save.txt; 100-104 are in-level effects -- accepted if written by hand. */
static const struct { int id; const char *name; } POWERUPS[] = {
  {  1, "Maelstrom" },       {  2, "SuperMonkeyStorm" }, {  3, "SpikeBallStorm" },
  {  4, "SupplyCrate" },     {  5, "Radar" },            {  6, "HeartStopper" },
  {  7, "PerishingPotions" },{  8, "Alchemy" },          {  9, "BallisticMissile" },
  { 10, "MonkeyAce" },       { 11, "PopAndAwe" },        { 12, "Phoenix" },
  { 13, "LightningStorm" },  { 14, "TheAntiBloon" },     { 15, "InstaMegas" },
  {100, "TackBlast" },       {101, "BombExplosion" },    {102, "ArcaneBlasts" },
  {103, "VerticalLine" },    {104, "HorizontalLine" },
};
#define N_PU ((int)(sizeof POWERUPS / sizeof *POWERUPS))

/* -1 = not set: leave the game's value alone. */
static long long g_pu[N_PU];
static long long g_lives = -1, g_money = -1, g_stones = -1, g_favours = -1;
static int g_can1 = -1, g_can2 = -1, g_autoff = -1, g_any;

/* ------------------------------------------------------------------ */
/* save.txt                                                            */
/* ------------------------------------------------------------------ */
static void write_template(const char *path) {
  FILE *f = fopen(path, "w");
  if (!f) { debugPrintf("[save] could not write %s\n", path); return; }
  fputs(
    "# save.txt -- edit your Bloons Pop save at boot.\n"
    "#\n"
    "# Remove the '#' in front of a line and set the number. The value is written\n"
    "# into your save every time the game starts, for as long as the line stays\n"
    "# uncommented. Lines that stay commented leave the game's own value alone.\n"
    "#\n"
    "# Your original save is kept once as Profile.Save.orig next to it. Close the\n"
    "# game fully before editing; changes apply at the next launch.\n"
    "\n"
    "# --- lives -----------------------------------------------------------\n"
    "# The game's own maximum is 5; higher values work but the UI is drawn for 5.\n"
    "#lives = 5\n"
    "\n"
    "# --- currencies ------------------------------------------------------\n"
    "#monkey_money = 1000\n"
    "#bloonstones = 100\n"
    "#party_favours = 100\n"
    "\n"
    "# --- power-ups: how many of each you hold ----------------------------\n", f);
  for (int i = 0; i < N_PU; i++)
    if (POWERUPS[i].id < 100) fprintf(f, "#powerup.%s = 10\n", POWERUPS[i].name);
  fputs(
    "#\n"
    "# In-level effects (ids 100-104: TackBlast, BombExplosion, ArcaneBlasts,\n"
    "# VerticalLine, HorizontalLine) are not listed, but powerup.<Name> or\n"
    "# powerup.<id> is accepted for them if written by hand.\n"
    "\n"
    "# --- bloontonium -----------------------------------------------------\n"
    "# The extra canister slots the store sells, first then second. If the second\n"
    "# does not show up on its own, set the first as well.\n"
    "#first_extra_canister = on\n"
    "#second_extra_canister = on\n"
    "\n"
    "# --- auto fast-forward -----------------------------------------------\n"
    "# The Auto Fast-Forward unlock the store sells (isAutoFFPurchased).\n"
    "#auto_ff_purchased = on\n", f);
  fclose(f);
  debugPrintf("[save] wrote template %s (every line commented out)\n", path);
}

/* Settings added after the first builds: a save.txt written by an older build
 * that has no line for one gets its block appended (once), so new options show
 * up without deleting the file. A line counts if it is "key =" or "#key =". */
static const struct { const char *keys[2]; const char *block; } ADDED[] = {
  { { "auto_ff_purchased", "isAutoFFPurchased" },
    "# --- auto fast-forward -----------------------------------------------\n"
    "# The Auto Fast-Forward unlock the store sells (isAutoFFPurchased).\n"
    "#auto_ff_purchased = on\n" },
};
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
  for (size_t i = 0; i < sizeof ADDED / sizeof *ADDED; i++) {
    if (mentions_key(text, ADDED[i].keys[0]) || mentions_key(text, ADDED[i].keys[1])) continue;
    if (!(f = fopen(path, "a"))) return;
    fprintf(f, "%s\n%s", (n && text[n - 1] != '\n') ? "\n" : "", ADDED[i].block);
    fclose(f);
    debugPrintf("[save] added the new \"%s\" option to %s\n", ADDED[i].keys[0], path);
  }
}

static void trim(char *s) {
  char *p = s;
  while (*p && isspace((unsigned char)*p)) p++;
  if (p != s) memmove(s, p, strlen(p) + 1);
  size_t n = strlen(s);
  while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}
static int parse_count(const char *key, const char *v, long long *out) {
  char *end;
  long long x = strtoll(v, &end, 10);
  if (end == v || *end) { debugPrintf("[save] %s: \"%s\" is not a number -- ignored\n", key, v); return 0; }
  if (x < 0) x = 0;
  if (x > 2147483647LL) x = 2147483647LL;               /* the fields are C# int */
  *out = x;
  return 1;
}
static int parse_bool(const char *key, const char *v, int *out) {
  if (!strcmp(v, "on") || !strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "1"))  { *out = 1; return 1; }
  if (!strcmp(v, "off") || !strcmp(v, "false") || !strcmp(v, "no") || !strcmp(v, "0")) { *out = 0; return 1; }
  debugPrintf("[save] %s: \"%s\" is not on/off -- ignored\n", key, v);
  return 0;
}

static int read_config(void) {
  char path[640], line[256];
  for (int i = 0; i < N_PU; i++) g_pu[i] = -1;
  snprintf(path, sizeof path, "%s/save.txt", bp_game_root());
  FILE *f = fopen(path, "r");
  if (!f) { write_template(path); return 0; }
  fclose(f);
  append_missing(path);
  if (!(f = fopen(path, "r"))) return 0;
  while (fgets(line, sizeof line, f)) {
    char *hash = strchr(line, '#');
    if (hash) *hash = 0;                                  /* '#' starts a comment anywhere */
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char *key = line, *val = eq + 1;
    trim(key); trim(val);
    if (!*key || !*val) continue;
    if      (!strcmp(key, "lives"))                 g_any |= parse_count(key, val, &g_lives);
    else if (!strcmp(key, "monkey_money"))          g_any |= parse_count(key, val, &g_money);
    else if (!strcmp(key, "bloonstones"))           g_any |= parse_count(key, val, &g_stones);
    else if (!strcmp(key, "party_favours"))         g_any |= parse_count(key, val, &g_favours);
    else if (!strcmp(key, "first_extra_canister"))  g_any |= parse_bool(key, val, &g_can1);
    else if (!strcmp(key, "second_extra_canister")) g_any |= parse_bool(key, val, &g_can2);
    else if (!strcmp(key, "auto_ff_purchased") || !strcmp(key, "isAutoFFPurchased"))
                                                    g_any |= parse_bool(key, val, &g_autoff);
    else if (!strncmp(key, "powerup.", 8)) {
      const char *w = key + 8;
      int i;
      for (i = 0; i < N_PU; i++)
        if (!strcasecmp(w, POWERUPS[i].name) || (isdigit((unsigned char)*w) && atoi(w) == POWERUPS[i].id)) break;
      if (i < N_PU) g_any |= parse_count(key, val, &g_pu[i]);
      else debugPrintf("[save] unknown power-up \"%s\" -- ignored\n", w);
    } else {
      debugPrintf("[save] unknown setting \"%s\" -- ignored\n", key);
    }
  }
  fclose(f);
  return g_any;
}

/* ------------------------------------------------------------------ */
/* crypto: PBKDF2-HMAC-SHA1, AES-128-CBC (libnx)                       */
/* ------------------------------------------------------------------ */
static const char *password_for(unsigned long long version) {
  switch (version) {
    case 0: return "01/01/0001 00:00:00";                /* DateTime(19 ticks).ToString(invariant) */
    case 1: return "01/01/0001 00:19:00";                /* DateTime(0) + 19 minutes */
    case 2: return "19";                                  /* appID.ToString() */
    default: return NULL;
  }
}
static void pbkdf2_sha1(const char *pw, const u8 *salt, size_t slen, int iters, u8 *out, size_t outlen) {
  u8 in[64], u[20], v[20], t[20];
  const size_t pl = strlen(pw);
  u32 block = 1;
  for (size_t done = 0; done < outlen; block++) {
    memcpy(in, salt, slen);
    in[slen] = (u8)(block >> 24); in[slen + 1] = (u8)(block >> 16);
    in[slen + 2] = (u8)(block >> 8); in[slen + 3] = (u8)block;
    hmacSha1CalculateMac(u, pw, pl, in, slen + 4);
    memcpy(t, u, 20);
    for (int j = 1; j < iters; j++) {
      hmacSha1CalculateMac(v, pw, pl, u, 20);
      memcpy(u, v, 20);
      for (int k = 0; k < 20; k++) t[k] ^= u[k];
    }
    const size_t n = outlen - done < 20 ? outlen - done : 20;
    memcpy(out + done, t, n);
    done += n;
  }
}
static void derive(unsigned long long version, const u8 salt[24], u8 key[16], u8 iv[16]) {
  u8 dk[32];
  pbkdf2_sha1(password_for(version), salt, 24, 10, dk, 32);
  memcpy(iv, dk, 16);                                     /* first GetBytes(16) -> IV  */
  memcpy(key, dk + 16, 16);                               /* second GetBytes(16) -> Key */
}

/* ------------------------------------------------------------------ */
/* container                                                           */
/* ------------------------------------------------------------------ */
/* Decode a Profile.Save. On success *hdr_len covers version+length+record,
 * *json is malloc'd (NUL-terminated, BOM kept) and 1 is returned. */
static int save_decode(const u8 *b, size_t n, size_t *hdr_len, unsigned long long *pwver,
                       char **json, size_t *jlen, const char **why) {
  int32_t ver, rlen;
  if (n < 8) { *why = "too short"; return 0; }
  memcpy(&ver, b, 4); memcpy(&rlen, b + 4, 4);
  if (ver != 1 || rlen < 0 || rlen > 256 || (size_t)(8 + rlen + 32) > n) { *why = "unexpected header"; return 0; }
  const size_t h = 8 + (size_t)rlen;
  unsigned long long pv;
  memcpy(&pv, b + h, 8);
  if (!password_for(pv)) { *why = "unknown password version"; return 0; }
  const u8 *salt = b + h + 8, *ct = b + h + 32;
  const size_t clen = n - h - 32;
  if (!clen || clen % 16) { *why = "ciphertext is not whole AES blocks"; return 0; }
  u8 key[16], iv[16];
  derive(pv, salt, key, iv);
  u8 *pt = malloc(clen);
  if (!pt) { *why = "out of memory"; return 0; }
  Aes128CbcContext ctx;
  aes128CbcContextCreate(&ctx, key, iv, false);
  aes128CbcDecrypt(&ctx, pt, ct, clen);
  const u8 pad = pt[clen - 1];
  int ok = pad >= 1 && pad <= 16;
  for (int i = 1; ok && i <= pad; i++) ok = pt[clen - i] == pad;
  if (!ok) { free(pt); *why = "bad padding (wrong key?)"; return 0; }
  z_stream zs; memset(&zs, 0, sizeof zs);
  if (inflateInit(&zs) != Z_OK) { free(pt); *why = "zlib init"; return 0; }
  size_t cap = 1u << 16, len = 0;
  char *out = malloc(cap + 1);
  zs.next_in = pt; zs.avail_in = (uInt)(clen - pad);
  int zr = Z_OK;
  while (out && zr == Z_OK) {
    if (len == cap) {
      if (cap >= MAX_SAVE * 8u) break;
      cap *= 2;
      char *nb = realloc(out, cap + 1);
      if (!nb) { free(out); out = NULL; break; }
      out = nb;
    }
    zs.next_out = (Bytef *)out + len; zs.avail_out = (uInt)(cap - len);
    zr = inflate(&zs, Z_NO_FLUSH);
    len = cap - zs.avail_out;
  }
  inflateEnd(&zs);
  free(pt);
  if (!out || zr != Z_STREAM_END) { free(out); *why = "zlib stream"; return 0; }
  out[len] = 0;
  *hdr_len = h; *pwver = pv; *json = out; *jlen = len;
  return 1;
}

static int save_encode(const u8 *hdr, size_t h, unsigned long long pv, const char *json, size_t jlen,
                       u8 **blob, size_t *blen) {
  uLongf zcap = compressBound((uLong)jlen);
  u8 *z = malloc(zcap + 16);
  if (!z || compress2(z, &zcap, (const Bytef *)json, (uLong)jlen, Z_DEFAULT_COMPRESSION) != Z_OK) { free(z); return 0; }
  const size_t pad = 16 - (zcap % 16);
  memset(z + zcap, (int)pad, pad);
  const size_t clen = zcap + pad;
  const size_t n = h + 32 + clen;
  u8 *b = malloc(n);
  if (!b) { free(z); return 0; }
  memcpy(b, hdr, h);
  if (h == 44) {                                          /* FileFormatV1: what a real save does */
    int32_t count; memcpy(&count, b + 8, 4); count++; memcpy(b + 8, &count, 4);
    const unsigned long long ticks = (unsigned long long)time(NULL) * 10000000ULL + 621355968000000000ULL;
    const unsigned long long modified = ticks | (1ULL << 62);          /* DateTimeKind.Utc */
    memcpy(b + 36, &modified, 8);
  }
  memcpy(b + h, &pv, 8);
  u8 *salt = b + h + 8, key[16], iv[16];
  randomGet(salt, 24);
  derive(pv, salt, key, iv);
  Aes128CbcContext ctx;
  aes128CbcContextCreate(&ctx, key, iv, true);
  aes128CbcEncrypt(&ctx, b + h + 32, z, clen);
  free(z);
  *blob = b; *blen = n;
  return 1;
}

/* ------------------------------------------------------------------ */
/* JSON: top-level keys only                                           */
/* ------------------------------------------------------------------ */
static size_t skip_string(const char *j, size_t n, size_t p) {     /* p at the opening quote */
  for (p++; p < n; p++) {
    if (j[p] == '\\') { p++; continue; }
    if (j[p] == '"') return p + 1;
  }
  return n;
}
static size_t skip_value(const char *j, size_t n, size_t p) {
  if (p >= n) return n;
  if (j[p] == '"') return skip_string(j, n, p);
  if (j[p] == '{' || j[p] == '[') {
    int depth = 0;
    for (; p < n; p++) {
      if (j[p] == '"') { p = skip_string(j, n, p) - 1; continue; }
      if (j[p] == '{' || j[p] == '[') depth++;
      else if ((j[p] == '}' || j[p] == ']') && --depth == 0) return p + 1;
    }
    return n;
  }
  while (p < n && j[p] != ',' && j[p] != '}' && j[p] != ']' && !isspace((unsigned char)j[p])) p++;
  return p;
}
/* Value extent of top-level `key`, or 0 if the save has no such key. */
static int top_value(const char *j, size_t n, const char *key, size_t *vs, size_t *ve) {
  size_t p = 0;
  while (p < n && j[p] != '{') p++;                       /* skip the BOM */
  if (p >= n) return 0;
  const size_t kl = strlen(key);
  for (p++; p < n; ) {
    while (p < n && (isspace((unsigned char)j[p]) || j[p] == ',')) p++;
    if (p >= n || j[p] == '}') return 0;
    if (j[p] != '"') return 0;
    const size_t ks = p + 1, ke = skip_string(j, n, p) - 1;
    p = ke + 1;
    while (p < n && isspace((unsigned char)j[p])) p++;
    if (p >= n || j[p] != ':') return 0;
    p++;
    while (p < n && isspace((unsigned char)j[p])) p++;
    const size_t v0 = p, v1 = skip_value(j, n, p);
    if (ke - ks == kl && !memcmp(j + ks, key, kl)) { *vs = v0; *ve = v1; return 1; }
    p = v1;
  }
  return 0;
}
static int splice(char **j, size_t *n, size_t s, size_t e, const char *rep) {
  const size_t rl = strlen(rep), nn = *n - (e - s) + rl;
  char *b = malloc(nn + 1);
  if (!b) return 0;
  memcpy(b, *j, s); memcpy(b + s, rep, rl); memcpy(b + s + rl, *j + e, *n - e);
  b[nn] = 0;
  free(*j); *j = b; *n = nn;
  return 1;
}

static char g_changes[1024];
static void note(const char *fmt, const char *field, const char *from, const char *to) {
  size_t l = strlen(g_changes);
  snprintf(g_changes + l, sizeof g_changes - l, fmt, l ? ", " : "", field, from, to);
}
static void set_int(char **j, size_t *n, const char *key, long long v) {
  size_t s, e;
  if (v < 0) return;
  if (!top_value(*j, *n, key, &s, &e)) { debugPrintf("[save] the save has no \"%s\" -- left alone\n", key); return; }
  char old[32], rep[32];
  snprintf(old, sizeof old, "%.*s", (int)(e - s), *j + s);
  snprintf(rep, sizeof rep, "%lld", v);
  if (strcmp(old, rep) && splice(j, n, s, e, rep)) note("%s%s %s->%s", key, old, rep);
}
static void set_bool(char **j, size_t *n, const char *key, int v) {
  size_t s, e;
  if (v < 0) return;
  if (!top_value(*j, *n, key, &s, &e)) { debugPrintf("[save] the save has no \"%s\" -- left alone\n", key); return; }
  char old[16];
  snprintf(old, sizeof old, "%.*s", (int)(e - s), *j + s);
  const char *rep = v ? "true" : "false";
  if (strcmp(old, rep) && splice(j, n, s, e, rep)) note("%s%s %s->%s", key, old, rep);
}
/* powerUpsCount is Dictionary<PowerUpType,int>; this game's Newtonsoft writes
 * enum dictionary keys as NUMBERS (see skippedMonkeyTypesCount). Existing
 * entries keep their order; new ones are appended in id order. */
static void set_powerups(char **j, size_t *n) {
  int want = 0;
  for (int i = 0; i < N_PU; i++) if (g_pu[i] >= 0) want = 1;
  if (!want) return;
  size_t s, e;
  if (!top_value(*j, *n, "powerUpsCount", &s, &e) || (*j)[s] != '{') {
    debugPrintf("[save] the save has no \"powerUpsCount\" object -- left alone\n");
    return;
  }
  int ids[64]; long long vals[64]; int m = 0;
  for (size_t p = s + 1; p < e && m < 64; ) {
    while (p < e && (isspace((unsigned char)(*j)[p]) || (*j)[p] == ',')) p++;
    if (p >= e || (*j)[p] == '}' || (*j)[p] != '"') break;
    ids[m] = atoi(*j + p + 1);
    p = skip_string(*j, e, p);
    while (p < e && ((*j)[p] == ':' || isspace((unsigned char)(*j)[p]))) p++;
    vals[m++] = strtoll(*j + p, NULL, 10);
    p = skip_value(*j, e, p);
  }
  int changed = 0;
  for (int i = 0; i < N_PU; i++) {
    if (g_pu[i] < 0) continue;
    int k;
    for (k = 0; k < m && ids[k] != POWERUPS[i].id; k++) {}
    if (k < m) { if (vals[k] != g_pu[i]) { vals[k] = g_pu[i]; changed = 1; } }
    else if (m < 64) { ids[m] = POWERUPS[i].id; vals[m++] = g_pu[i]; changed = 1; }
  }
  if (!changed) return;
  char rep[64 * 24] = "{";
  for (int k = 0; k < m; k++) {
    size_t l = strlen(rep);
    snprintf(rep + l, sizeof rep - l, "%s\"%d\":%lld", k ? "," : "", ids[k], vals[k]);
  }
  strncat(rep, "}", sizeof rep - strlen(rep) - 1);
  char old[48];
  snprintf(old, sizeof old, "%.*s%s", (int)((e - s) < 40 ? (e - s) : 40), *j + s, (e - s) > 40 ? "..." : "");
  if (splice(j, n, s, e, rep)) note("%s%s %s->%s", "powerUpsCount", old, rep);
}

/* ------------------------------------------------------------------ */
/* one save file                                                       */
/* ------------------------------------------------------------------ */
static u8 *read_file(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
  u8 *b = (sz > 0 && (unsigned long)sz <= MAX_SAVE) ? malloc((size_t)sz) : NULL;
  if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
  fclose(f);
  *n = b ? (size_t)sz : 0;
  return b;
}
static int write_file(const char *path, const u8 *b, size_t n) {
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  const int ok = fwrite(b, 1, n, f) == n;
  return fclose(f) == 0 && ok;
}

static void patch_save(const char *path) {
  size_t n = 0, h = 0, jl = 0;
  unsigned long long pv = 0;
  char *json = NULL;
  const char *why = "";
  u8 *orig = read_file(path, &n);
  if (!orig) { debugPrintf("[save] cannot read %s\n", path); return; }
  if (!save_decode(orig, n, &h, &pv, &json, &jl, &why)) {
    debugPrintf("[save] %s: not decodable (%s) -- left alone\n", path, why);
    free(orig);
    return;
  }
  g_changes[0] = 0;
  set_int(&json, &jl, "lives", g_lives);
  set_int(&json, &jl, "monkeyMoney", g_money);
  set_int(&json, &jl, "bloonStones", g_stones);
  set_int(&json, &jl, "partyFavours", g_favours);
  set_bool(&json, &jl, "purchasedFirstExtraCanister", g_can1);
  set_bool(&json, &jl, "purchasedSecondExtraCanister", g_can2);
  set_bool(&json, &jl, "isAutoFFPurchased", g_autoff);
  set_powerups(&json, &jl);
  if (!g_changes[0]) {
    debugPrintf("[save] %s already matches save.txt\n", path);
    free(json); free(orig);
    return;
  }
  u8 *blob = NULL; size_t bl = 0;
  if (!save_encode(orig, h, pv, json, jl, &blob, &bl)) {
    debugPrintf("[save] %s: re-encoding failed -- left alone\n", path);
    free(json); free(orig);
    return;
  }
  size_t h2, jl2; unsigned long long pv2; char *check = NULL;         /* verify before writing */
  if (!save_decode(blob, bl, &h2, &pv2, &check, &jl2, &why) || jl2 != jl || memcmp(check, json, jl)) {
    debugPrintf("[save] %s: verification of the new save FAILED (%s) -- left alone\n", path, why);
    free(check); free(blob); free(json); free(orig);
    return;
  }
  free(check);
  char aux[700];
  struct stat st;
  snprintf(aux, sizeof aux, "%s.orig", path);
  if (stat(aux, &st) != 0) {
    if (!write_file(aux, orig, n)) {
      debugPrintf("[save] could not write the backup %s -- not editing without it\n", aux);
      free(blob); free(json); free(orig);
      return;
    }
    debugPrintf("[save] original kept as %s\n", aux);
  }
  snprintf(aux, sizeof aux, "%s.tmp", path);
  int ok = write_file(aux, blob, bl);
  if (ok) {
    remove(path);
    ok = rename(aux, path) == 0 || write_file(path, blob, bl);
    remove(aux);
  }
  fsdevCommitDevice("sdmc");
  debugPrintf("[save] %s %s: %s\n", path, ok ? "updated" : "WRITE FAILED", g_changes);
  free(blob); free(json); free(orig);
}

static int find_saves(const char *dir, int depth) {
  DIR *d = opendir(dir);
  if (!d) return 0;
  int found = 0;
  struct dirent *e;
  char p[700];
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.') continue;
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    struct stat st;
    if (stat(p, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      if (depth < 6 && strcmp(e->d_name, "UnityCache")) found += find_saves(p, depth + 1);
    } else if (!strcmp(e->d_name, "Profile.Save")) {
      patch_save(p);
      found++;
    }
  }
  closedir(d);
  return found;
}

void bp_savetool_run(void) {
  if (!read_config()) return;                             /* nothing uncommented */
  char files[640];
  snprintf(files, sizeof files, "%s/files", bp_game_root());
  if (!find_saves(files, 0))
    debugPrintf("[save] save.txt has settings, but there is no Profile.Save under %s yet "
                "(it appears after the first play session)\n", files);
}
