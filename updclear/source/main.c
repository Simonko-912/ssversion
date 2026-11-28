// updclear v1.3: installed-title list + per-title diagnostic so we can
// see exactly what ns answers at launch time. Selecting a title runs
// the same check a game does (RequestApplicationUpdateInfo, async) and
// prints ns's answer plus the cached version numbers around it. A then
// withdraws the update request (cmd 907) and re-runs the check so the
// before/after is visible on screen. Pure IPC, ns stays alive.
// One source for Switch 1 (libnx) and Switch 2 (libounce).

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#define MAXT 128

typedef struct {
  u64 tid;
  char name[64];
  char meta[4][24];
  int nmeta;
  u32 appVer;
  bool req;
  char dbg[96];   // rc codes from the per-title queries
} Title;

static Title titles[MAXT];
static int count = 0;
static int cursor = 0;
static int top = 0;

// last diagnostic run for the selected title
static char diag[512];

static void fmtVer(char *out, size_t n, u32 v) {
  snprintf(out, n, "%u.%u.%u.%u", (v >> 26) & 0x3FFF, (v >> 20) & 0x3F,
           (v >> 14) & 0x3F, v & 0x3FFF);
}

static void loadTitles(void) {
  count = 0;
  s32 off = 0;
  while (count < MAXT) {
    NsApplicationRecord recs[32];
    s32 got = 0;
    Result rc = nsListApplicationRecord(recs, 32, off, &got);
    if (R_FAILED(rc) || got <= 0) break;
    for (s32 i = 0; i < got && count < MAXT; i++) {
      u64 tid = recs[i].application_id;
      Title *t = &titles[count++];
      t->tid = tid;
      t->name[0] = 0;
      t->nmeta = 0;
      t->appVer = 0;
      t->req = false;

      s32 n = 0;
      Result crc = nsCountApplicationContentMeta(tid, &n);
      Result lrc = 0;
      s32 one = 0;
      if (R_SUCCEEDED(crc) && n > 0) {
        NsApplicationContentMetaStatus st0;
        lrc = nsListApplicationContentMetaStatus(tid, 0, &st0, 1, &one);
      }
      snprintf(t->dbg, sizeof(t->dbg), "cnt=%d rc=0x%x lrc=0x%x n=%d",
               n, crc, lrc, one);
      if (n > 4) n = 4;
      for (s32 k = 0; k < n; k++) {
        NsApplicationContentMetaStatus st;
        s32 oo = 0;
        if (R_SUCCEEDED(nsListApplicationContentMetaStatus(tid, k, &st, 1,
                                                           &oo)) &&
            oo == 1) {
          char vs[24];
          fmtVer(vs, sizeof(vs), st.version);
          snprintf(t->meta[t->nmeta++], sizeof(t->meta[0]), "%02x:%s",
                   st.meta_type, vs);
          if (st.meta_type == NcmContentMetaType_Application &&
              st.version >= t->appVer)
            t->appVer = st.version;
        }
      }

      bool req = false;
      u32 fl = 0;
      Result frc = nsIsApplicationUpdateRequested(tid, &req, &fl);
      t->req = req || fl != 0;
      {
        size_t used = strlen(t->dbg);
        snprintf(t->dbg + used, sizeof(t->dbg) - used, " f=0x%x", frc);
      }

      static NsApplicationControlData cd;
      u64 have = 0;
      if (R_SUCCEEDED(nsGetApplicationControlData(
              NsApplicationControlSource_CacheOnly, tid, &cd, sizeof(cd),
              &have)) &&
          have)
        snprintf(t->name, sizeof(t->name), "%s", (const char *)&cd.nacp);
      if (!t->name[0])
        snprintf(t->name, sizeof(t->name), "%016llx", (unsigned long long)tid);
    }
    off += got;
    if (got < 32) break;
  }
}

// The launch-time check itself: RequestApplicationUpdateInfo returns an
// AsyncValue whose payload is what the applet shows. Dump first bytes.
static void runCheck(int i) {
  diag[0] = 0;
  if (i < 0 || i >= count) return;
  Title *t = &titles[i];
  char *p = diag;
  size_t n = sizeof(diag);

  AsyncValue av;
  memset(&av, 0, sizeof(av));
  Result rc = nsRequestApplicationUpdateInfo(&av, t->tid);
  p += snprintf(p, n - (size_t)(p - diag), "check rc=0x%x", rc);
  if (R_FAILED(rc)) {
    snprintf(p, n - (size_t)(p - diag), "\n");
    return;
  }

  rc = asyncValueWait(&av, ~(u64)0);
  p += snprintf(p, n - (size_t)(p - diag), " wait=0x%x", rc);
  if (R_SUCCEEDED(rc)) {
    u64 size = 0;
    asyncValueGetSize(&av, &size);
    u8 buf[64];
    if (size > sizeof(buf)) size = sizeof(buf);
    rc = asyncValueGet(&av, buf, size);
    p += snprintf(p, n - (size_t)(p - diag), " size=%llu get=0x%x\n",
                  (unsigned long long)size, rc);
    if (R_SUCCEEDED(rc) && size > 0)
      p += snprintf(p, n - (size_t)(p - diag), "answer=%u\n", buf[0]);
    p += snprintf(p, n - (size_t)(p - diag), "data:");
    if (R_SUCCEEDED(rc))
      for (u64 k = 0; k < size && k < 24; k++)
        p += snprintf(p, n - (size_t)(p - diag), "%02x ", buf[k]);
  }
  asyncValueClose(&av);
  snprintf(p, n - (size_t)(p - diag), "\n");
}

static void refreshFlag(int i) {
  bool req = false;
  u32 fl = 0;
  nsIsApplicationUpdateRequested(titles[i].tid, &req, &fl);
  titles[i].req = req || fl != 0;
}

static void withdrawOne(int i) {
  if (i < 0 || i >= count) return;
  nsWithdrawApplicationUpdateRequest(titles[i].tid);
  refreshFlag(i);
  runCheck(i);
}

static void withdrawAllFlags(void) {
  for (int i = 0; i < count; i++)
    if (titles[i].req) {
      nsWithdrawApplicationUpdateRequest(titles[i].tid);
      refreshFlag(i);
    }
  runCheck(cursor);
}

static void draw(void) {
  consoleClear();
  int flagged = 0;
  for (int i = 0; i < count; i++) flagged += titles[i].req;
  printf("updclear v1.4 - %d titles, %d flagged\n\n", count, flagged);

  int rows = 14;
  if (cursor < top) top = cursor;
  if (cursor >= top + rows) top = cursor - rows + 1;
  for (int r = 0; r < rows && top + r < count; r++) {
    int i = top + r;
    char vs[24];
    fmtVer(vs, sizeof(vs), titles[i].appVer);
    printf("%c %s %s%s\n", i == cursor ? '>' : ' ', titles[i].name, vs,
           titles[i].req ? " [*]" : "");
  }

  if (cursor < count) {
    Title *t = &titles[cursor];
    printf("%s\n", t->dbg);
    printf("meta:");
    for (int k = 0; k < t->nmeta; k++) printf(" %s", t->meta[k]);
    if (!t->nmeta) printf(" none");
    printf("\n%s", diag);
  }
  printf("\nA=withdraw+recheck  X=all  Y=recheck  Plus=exit\n");
}

int main(int argc, char *argv[]) {
  consoleInit(NULL);
  Result rc = nsInitialize();
  if (R_FAILED(rc)) { printf("ns:0x%x\n", rc); return 0; }

  loadTitles();
  runCheck(cursor);

  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);

  bool dirty = true;
  while (appletMainLoop()) {
    padUpdate(&pad);
    u64 d = padGetButtonsDown(&pad);
    if (dirty) { draw(); dirty = false; }
    consoleUpdate(NULL);
    if (d & HidNpadButton_Plus) break;
    if (d & HidNpadButton_Up) {
      if (cursor) cursor--;
      runCheck(cursor);
      dirty = true;
    }
    if (d & HidNpadButton_Down) {
      if (cursor < count - 1) cursor++;
      runCheck(cursor);
      dirty = true;
    }
    if (d & HidNpadButton_L) {
      cursor -= 14;
      if (cursor < 0) cursor = 0;
      runCheck(cursor);
      dirty = true;
    }
    if (d & HidNpadButton_R) {
      cursor += 14;
      if (cursor >= count) cursor = count ? count - 1 : 0;
      runCheck(cursor);
      dirty = true;
    }
    if (d & HidNpadButton_A) { withdrawOne(cursor); dirty = true; }
    if (d & HidNpadButton_X) { withdrawAllFlags(); dirty = true; }
    if (d & HidNpadButton_Y) { runCheck(cursor); dirty = true; }
  }

  nsExit();
  consoleExit(NULL);
  return 0;
}
