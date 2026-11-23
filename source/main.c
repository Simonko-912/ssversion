// ssversion: dump the SystemUpdate ssversion SystemSaveData (id 0x49,
// ns_ssversion:/entry) as hex + parsed fields. Switch 2 / libounce.

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#define SAVE_ID_A 0x0000000000000049ULL
#define SAVE_ID_B 0x8000000000000049ULL
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

static Result openSsVersion(FsFileSystem *fs) {
  Result rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);
  static const FsSaveDataSpaceId spaces[] = {
      FsSaveDataSpaceId_ProperSystem,
      FsSaveDataSpaceId_System,
      FsSaveDataSpaceId_SafeMode,
  };
  static const u64 ids[] = { SAVE_ID_A, SAVE_ID_B };

  for (size_t si = 0; si < sizeof(spaces) / sizeof(spaces[0]); si++) {
    for (size_t ii = 0; ii < sizeof(ids) / sizeof(ids[0]); ii++) {
      FsSaveDataAttribute attr;
      memset(&attr, 0, sizeof(attr));
      attr.application_id = FS_SAVEDATA_CURRENT_APPLICATIONID;
      attr.system_save_data_id = ids[ii];
      attr.save_data_type = FsSaveDataType_System;
      attr.save_data_rank = FsSaveDataRank_Primary;

      rc = fsOpenSaveDataFileSystemBySystemSaveDataId(fs, spaces[si], &attr);
      if (R_SUCCEEDED(rc)) {
        printf("opened space %d id %016llx\n", (int)spaces[si],
               (unsigned long long)ids[ii]);
        return rc;
      }
    }
  }
  return rc;
}

int main(int argc, char *argv[]) {
  consoleInit(NULL);

  FsFileSystem fs;
  Result rc = openSsVersion(&fs);
  if (R_FAILED(rc)) {
    printf("fsOpenSaveDataFileSystemBySystemSaveDataId failed: 0x%x\n", rc);
    while (appletMainLoop()) {
      // wait for Plus to quit even on failure
      PadState pad;
      padInitializeDefault(&pad);
      padUpdate(&pad);
      if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
      consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
  }

  u8 buf[BUF_SIZE];
  u64 read = 0;
  FsFile file;
  // OpenMode_Read == 1; libounce's fs.h has no FS_OPEN_* macros.
  rc = fsFsOpenFile(&fs, ENTRY_PATH, 1, &file);
  if (R_SUCCEEDED(rc)) {
    rc = fsFileRead(&file, 0, buf, BUF_SIZE, FsReadOption_None, &read);
    fsFileClose(&file);
  }
  fsFsClose(&fs);

  if (R_FAILED(rc) || read == 0) {
    printf("open/read " ENTRY_PATH " failed: 0x%x (read %llu)\n", rc,
           (unsigned long long)read);
  } else {
    printf("== hex (%llu bytes) ==\n", (unsigned long long)read);
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
    printf("last 'no upd': %llu (%s)\n", (unsigned long long)last_no_update,
           t2);
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
