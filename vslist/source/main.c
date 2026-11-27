// vslist: dump ns's per-title version table over plain IPC (no ns
// terminate, no save mount). For every installed application it shows
// the installed app version and installed update (Patch) version from
// ns:am's ContentMeta list, plus ns' "update requested" flag - the
// exact inputs the launch-time "update available" dialog reads.
// A clears the requested-update flags, Plus exits.
// Builds for Switch 1 (libnx) and Switch 2 (libounce) from this file.

#include <stdio.h>
#include <string.h>

#include <switch.h>

// nn::cm::DisplayVersion packing inside the u32 ContentMeta version.
static void fmtVer(char *out, size_t n, u32 v) {
  u32 micro = v & 0xFF;
  u32 minor = (v >> 8) & 0x3F;
  u32 major = (v >> 14) & 0x3FFF;
  snprintf(out, n, "%u.%u.%u", major, minor, micro);
}

static const char *metaName(u8 t) {
  switch (t) {
    case NcmContentMetaType_Application: return "app";
    case NcmContentMetaType_Patch: return "patch";
    case NcmContentMetaType_AddOnContent: return "aoc";
    case NcmContentMetaType_Delta: return "delta";
    case NcmContentMetaType_DataPatch: return "datapat";
    default: return "meta";
  }
}

// ns:am ContentMeta status entries for one title: entry 0 is the app,
// later entries are installed updates/AOC. This is the version table.
static void dumpTitle(u64 tid) {
  s32 count = 0;
  Result rc = nsCountApplicationContentMeta(tid, &count);
  if (R_FAILED(rc)) {
    printf("%016llx count:0x%x\n", (unsigned long long)tid, rc);
    return;
  }

  bool requested = false;
  u32 rflags = 0;
  Result wrc = nsIsApplicationUpdateRequested(tid, &requested, &rflags);

  printf("%016llx", (unsigned long long)tid);
  if (R_SUCCEEDED(wrc) && requested) printf("  UPD-REQ");
  printf("\n");

  NsApplicationContentMetaStatus list[8];
  s32 off = 0;
  while (off < count) {
    s32 got = 0;
    s32 want = count - off;
    if (want > 8) want = 8;
    rc = nsListApplicationContentMetaStatus(tid, off, list, want, &got);
    if (R_FAILED(rc) || got <= 0) {
      printf("  list:0x%x\n", rc);
      return;
    }
    for (s32 i = 0; i < got; i++) {
      char ver[24];
      fmtVer(ver, sizeof(ver), list[i].version);
      printf("  %-7s st=%d v%s (0x%08x)\n", metaName(list[i].meta_type),
             (int)list[i].storageID, ver, list[i].version);
    }
    off += got;
  }
}

int main(int argc, char *argv[]) {
  consoleInit(NULL);

  Result rc = nsInitialize();
  printf("vslist v1.0   hos=0x%x   ns:0x%x\n\n", hosversionGet(), rc);

  PadState pad;
  padInitializeDefault(&pad);

  bool redraw = true;
  while (appletMainLoop()) {
    if (redraw) {
      consoleClear();
      printf("vslist v1.0   hos=0x%x\n\n", hosversionGet());

      NsApplicationRecord recs[0x20];
      s32 off = 0;
      int total = 0;
      while (1) {
        s32 got = 0;
        rc = nsListApplicationRecord(recs, 0x20, off, &got);
        if (R_FAILED(rc) || got <= 0) break;
        for (s32 i = 0; i < got; i++) {
          dumpTitle(recs[i].application_id);
          total++;
        }
        off += got;
        if (got < 0x20) break;
      }
      printf("\n%d titles   A=clear UPD-REQ   Plus=exit\n", total);
      redraw = false;

      if (R_FAILED(rc) && rc != (Result)MAKERESULT(Module_Libnx, LibnxError_NotFound))
        printf("list rc=0x%x\n", rc);
    }

    padUpdate(&pad);
    u64 down = padGetButtonsDown(&pad);
    if (down & HidNpadButton_Plus) break;
    if (down & HidNpadButton_A) {
      // Drop ns' pending update-request flags so the launch dialog
      // stops for titles that already have a queued nag.
      NsApplicationRecord recs[0x20];
      s32 off = 0;
      while (1) {
        s32 got = 0;
        if (R_FAILED(nsListApplicationRecord(recs, 0x20, off, &got)) ||
            got <= 0)
          break;
        for (s32 i = 0; i < got; i++)
          nsWithdrawApplicationUpdateRequest(recs[i].application_id);
        off += got;
        if (got < 0x20) break;
      }
      redraw = true;
    }
    consoleUpdate(NULL);
  }

  nsExit();
  consoleExit(NULL);
  return 0;
}
