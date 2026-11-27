// updclear: list installed titles, pick one, clear its queued update
// request (WithdrawApplicationUpdateRequest, ns cmd 907) so the polite
// "update available" dialog stops for that title. ns stays alive; pure
// IPC, no terminate, no reboot. X clears every flagged title at once.
// One source for Switch 1 (libnx) and Switch 2 (libounce).

#include <stdio.h>
#include <string.h>

#include <switch.h>

#define MAXT 128

typedef struct {
  u64 tid;
  char name[64];
  bool req;
} Title;

static Title titles[MAXT];
static int count = 0;
static int cursor = 0;
static int top = 0;

static void fmtVer(char *out, size_t n, u32 v) {
  snprintf(out, n, "%u.%u.%u", (v >> 26) & 0x3FFF, (v >> 20) & 0x3F,
           (v >> 14) & 0x3F);
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
      t->req = false;

      // installed version: highest app ContentMeta entry
      s32 n = 0, bestIdx = -1;
      u32 best = 0;
      nsCountApplicationContentMeta(tid, &n);
      for (s32 k = 0; k < n && k < 8; k++) {
        NsApplicationContentMetaStatus st;
        s32 one = 0;
        if (R_SUCCEEDED(nsListApplicationContentMetaStatus(tid, k, &st, 1,
                                                           &one)) &&
            one == 1 && st.meta_type == NcmContentMetaType_Application &&
            st.version >= best) {
          best = st.version;
          bestIdx = k;
        }
      }
      (void)bestIdx;

      bool req = false;
      u32 fl = 0;
      nsIsApplicationUpdateRequested(tid, &req, &fl);
      t->req = req || fl != 0;

      // friendly name from cache when present
      static NsApplicationControlData cd;
      u64 have = 0;
      if (R_SUCCEEDED(nsGetApplicationControlData(
              NsApplicationControlSource_CacheOnly, tid, &cd, sizeof(cd),
              &have)) &&
          have) {
        const char *en = ((const char *)&cd.nacp) + 0; // first lang name[0x200]
        snprintf(t->name, sizeof(t->name), "%s", en);
      }
      if (!t->name[0])
        snprintf(t->name, sizeof(t->name), "%016llx", (unsigned long long)tid);
    }
    off += got;
    if (got < 32) break;
  }
}

static void withdrawOne(int i) {
  if (i < 0 || i >= count) return;
  Result rc = nsWithdrawApplicationUpdateRequest(titles[i].tid);
  titles[i].req = R_FAILED(rc);
}

static void withdrawAllFlags(void) {
  for (int i = 0; i < count; i++)
    if (titles[i].req) withdrawOne(i);
}

static void draw(void) {
  consoleClear();
  printf("updclear - %d titles, %d flagged\n\n", count,
         ({ int c = 0; for (int i = 0; i < count; i++) c += titles[i].req;
            c; }));
  int rows = 20;
  if (cursor < top) top = cursor;
  if (cursor >= top + rows) top = cursor - rows + 1;
  for (int r = 0; r < rows && top + r < count; r++) {
    int i = top + r;
    printf("%c %s%s\n", i == cursor ? '>' : ' ', titles[i].name,
           titles[i].req ? "  [upd]" : "");
  }
  printf("\nA=clear selected  X=clear all flagged  R/L=page  Plus=exit\n");
}

int main(int argc, char *argv[]) {
  consoleInit(NULL);
  Result rc = nsInitialize();
  if (R_FAILED(rc)) { printf("ns:0x%x\n", rc); return 0; }

  loadTitles();

  PadState pad;
  padInitializeDefault(&pad);
  bool dirty = true;
  while (appletMainLoop()) {
    if (dirty) { draw(); consoleUpdate(NULL); dirty = false; }
    padUpdate(&pad);
    u64 d = padGetButtonsDown(&pad);
    if (d & HidNpadButton_Plus) break;
    if (d & HidNpadButton_Up) { if (cursor) cursor--; dirty = true; }
    if (d & HidNpadButton_Down) { if (cursor < count - 1) cursor++;
                                  dirty = true; }
    if (d & HidNpadButton_L) { cursor -= 20; if (cursor < 0) cursor = 0;
                              dirty = true; }
    if (d & HidNpadButton_R) { cursor += 20; if (cursor >= count)
                              cursor = count ? count - 1 : 0; dirty = true; }
    if (d & HidNpadButton_A) { withdrawOne(cursor); dirty = true; }
    if (d & HidNpadButton_X) { withdrawAllFlags(); dirty = true; }
  }

  nsExit();
  consoleExit(NULL);
  return 0;
}
