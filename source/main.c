// ssversion: dump the SystemUpdate ssversion SystemSaveData (id 0x49,
// ns_ssversion:/entry) as hex + parsed fields.
// Builds for Switch 1 (libnx) and Switch 2 (libounce) from this one file.

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#define OWNER_TID 0x0100000000000816ULL
#define ENTRY_PATH "/entry"
#define BUF_SIZE 0x40

static const char *stateName(u8 s) {
  switch (s) {
    case 0: return "unknown";
    case 1: return "network error";
    case 2: return "needs update";
    case 3: return "no update needed";
    default: return "?";
  }
}

static void fmtTime(char *out, size_t out_size, u64 secs) {
  time_t t = (time_t)secs;
  struct tm tmv;
  if (secs == 0) { snprintf(out, out_size, "(unset)"); return; }
  gmtime_r(&t, &tmv);
  strftime(out, out_size, "%Y-%m-%d %H:%M:%S UTC", &tmv);
}

// Open the ssversion SystemSaveData. The save may be registered under the
// plain id (System/SafeMode spaces) or with bit63 set (ProperSystem), and
// hori matches on the owner tid in the attribute too, so sweep the small
// matrix: space x id x application_id, printing every miss.
static Result openSsVersion(FsFileSystem *fs) {
  Result last = MAKERESULT(Module_Libnx, LibnxError_NotFound);
  static const FsSaveDataSpaceId spaces[] = {
      FsSaveDataSpaceId_ProperSystem,
      FsSaveDataSpaceId_System,
      FsSaveDataSpaceId_SafeMode,
  };
  static const u64 ids[] = { 0x49ULL, 0x8000000000000049ULL };
  static const u64 apps[] = { FS_SAVEDATA_CURRENT_APPLICATIONID, OWNER_TID };

  for (size_t si = 0; si < sizeof(spaces) / sizeof(spaces[0]); si++) {
    for (size_t ii = 0; ii < sizeof(ids) / sizeof(ids[0]); ii++) {
      for (size_t ai = 0; ai < sizeof(apps) / sizeof(apps[0]); ai++) {
        FsSaveDataAttribute attr;
        memset(&attr, 0, sizeof(attr));
        attr.application_id = apps[ai];
        attr.system_save_data_id = ids[ii];
        attr.save_data_type = FsSaveDataType_System;
        attr.save_data_rank = FsSaveDataRank_Primary;

        Result rc = fsOpenSaveDataFileSystemBySystemSaveDataId(
            fs, spaces[si], &attr);
        if (R_SUCCEEDED(rc)) {
          printf("opened: space=%d id=%016llx app=%016llx\n", (int)spaces[si],
                 (unsigned long long)ids[ii],
                 (unsigned long long)apps[ai]);
          return rc;
        }
        last = rc;
        printf("miss: space=%d id=%016llx app=%016llx rc=0x%x\n",
               (int)spaces[si], (unsigned long long)ids[ii],
               (unsigned long long)apps[ai], rc);
      }
    }
  }
  return last;
}

static void dumpEntry(FsFileSystem *fs) {
  u8 buf[BUF_SIZE];
  u64 read = 0;
  FsFile file;
  // OpenMode_Read == 1; works on libnx and libounce alike.
  Result rc = fsFsOpenFile(fs, ENTRY_PATH, 1, &file);
  if (R_SUCCEEDED(rc)) {
    rc = fsFileRead(&file, 0, buf, BUF_SIZE, FsReadOption_None, &read);
    fsFileClose(&file);
  }

  if (R_FAILED(rc) || read == 0) {
    printf("open/read " ENTRY_PATH " failed: 0x%x (read %llu)\n", rc,
           (unsigned long long)read);
    return;
  }

  printf("\n== hex (%llu bytes) ==\n", (unsigned long long)read);
  for (u64 i = 0; i < read; i++) {
    printf("%02x%s", buf[i], ((i & 3) == 3 || i == read - 1) ? "\n" : " ");
  }

  u64 tid = *(u64 *)(buf + 0x00);
  u32 supernag = *(u32 *)(buf + 0x08);
  u32 field0c = *(u32 *)(buf + 0x0C);
  u8 state = buf[0x10];
  u64 last_check = *(u64 *)(buf + 0x18);
  u8 major = buf[0x20], minor = buf[0x21], micro = buf[0x22];
  u8 time_set = buf[0x23];
  u64 last_no_update = *(u64 *)(buf + 0x28);

  char t1[40], t2[40];
  fmtTime(t1, sizeof(t1), last_check);
  fmtTime(t2, sizeof(t2), time_set ? last_no_update : 0);

  printf("\n== parsed ==\n");
  printf("title id     : %016llx\n", (unsigned long long)tid);
  printf("supernag     : 0x%08x\n", supernag);
  printf("0x0C         : %u\n", field0c);
  printf("state        : %u (%s)\n", state, stateName(state));
  printf("last check   : %llu (%s)\n", (unsigned long long)last_check, t1);
  printf("fw version   : %u.%u.%u\n", major, minor, micro);
  printf("time valid   : %u\n", time_set);
  printf("last 'no upd': %llu (%s)\n", (unsigned long long)last_no_update, t2);
}

int main(int argc, char *argv[]) {
  consoleInit(NULL);

  FsFileSystem fs;
  Result rc = openSsVersion(&fs);
  if (R_SUCCEEDED(rc)) {
    dumpEntry(&fs);
    fsFsClose(&fs);
  } else {
    printf("no matching SystemSaveData, last rc=0x%x\n", rc);
  }

  PadState pad;
  padInitializeDefault(&pad);
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    consoleUpdate(NULL);
  }
  consoleExit(NULL);
  return 0;
}
