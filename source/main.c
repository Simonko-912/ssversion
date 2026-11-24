// ssversion: dump the SystemUpdate ssversion SystemSaveData (id ...49,
// ns_ssversion:/entry) as hex + parsed fields.
// Builds for Switch 1 (libnx) and Switch 2 (libounce) from this one file.
//
// Strategy: enumerate SystemSaveData records with the save-info reader,
// print every record, then open the matching one with EXACTLY the
// attributes the reader returned (fsp-srv on Switch 1 matches every
// nonzero filter field, so hand-built attributes are unreliable).

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <switch.h>

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

static bool isSsVersionId(u64 id) {
  return id == 0x49ULL || id == 0x8000000000000049ULL;
}

static void printInfo(const FsSaveDataInfo *info) {
  u64 uid_hi = 0, uid_lo = 0;
  memcpy(&uid_hi, info->uid.uid, 8);
  memcpy(&uid_lo, info->uid.uid + 8, 8);
  printf("rec: space=%d type=%d rank=%d idx=%u ssid=%016llx app=%016llx"
         " uid=%016llx%016llx size=%lld\n",
         (int)info->save_data_space_id, (int)info->save_data_type,
         (int)info->save_data_rank, info->save_data_index,
         (unsigned long long)info->system_save_data_id,
         (unsigned long long)info->application_id,
         (unsigned long long)uid_hi, (unsigned long long)uid_lo,
         (long long)info->size);
}

// Enumerate all save spaces, print the records, fill 'picked' with the
// ssversion entry (or the first System-type entry as a fallback).
static bool findSsVersion(FsSaveDataInfo *picked) {
  FsSaveDataInfoReader reader;
  bool found = false;
  bool fallback_done = false;

  Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_All);
  if (R_FAILED(rc)) {
    printf("fsOpenSaveDataInfoReader: 0x%x\n", rc);
    return false;
  }

  FsSaveDataInfo info;
  s64 total = 0;
  do {
    total = 0;
    rc = fsSaveDataInfoReaderRead(&reader, &info, 1, &total);
    if (R_FAILED(rc) || total == 0) break;
    printInfo(&info);
    if (!found && isSsVersionId(info.system_save_data_id)) {
      *picked = info;
      found = true;
    } else if (!fallback_done && info.save_data_type == FsSaveDataType_System &&
               info.system_save_data_id != 0) {
      *picked = info; // keep a candidate in case the exact id is absent
      fallback_done = true;
    }
  } while (true);

  fsSaveDataInfoReaderClose(&reader);
  return found || fallback_done;
}

// Owner of the ssversion SystemSaveData: SystemUpdate (ns). The info
// reader reports app=0 for System-type saves, but fsp-srv still matches
// the owner internally, so retry with the real owner id if needed.
#define OWNER_TID 0x0100000000000816ULL

static Result openFromInfo(FsFileSystem *fs, const FsSaveDataInfo *info) {
  FsSaveDataAttribute attr;
  memset(&attr, 0, sizeof(attr));
  attr.application_id = info->application_id;
  attr.uid = info->uid;
  attr.system_save_data_id = info->system_save_data_id;
  attr.save_data_type = info->save_data_type;
  attr.save_data_rank = info->save_data_rank;
  attr.save_data_index = info->save_data_index;

  printf("opening: space=%d ssid=%016llx app=%016llx type=%d rank=%d\n",
         (int)info->save_data_space_id,
         (unsigned long long)info->system_save_data_id,
         (unsigned long long)info->application_id,
         (int)info->save_data_type, (int)info->save_data_rank);

  FsSaveDataSpaceId space = (FsSaveDataSpaceId)info->save_data_space_id;
  Result rc = fsOpenSaveDataFileSystemBySystemSaveDataId(fs, space, &attr);
  if (R_SUCCEEDED(rc)) {
    printf("opened: bySsid exact\n");
    return rc;
  }
  printf("attempt1 rc=0x%x\n", rc);

  // Retry with the real owner tid (SystemUpdate) + enumerated attrs.
  attr.application_id = OWNER_TID;
  rc = fsOpenSaveDataFileSystemBySystemSaveDataId(fs, space, &attr);
  if (R_SUCCEEDED(rc)) {
    printf("opened: bySsid owner+0100000000000816\n");
    return rc;
  }
  printf("attempt2 rc=0x%x\n", rc);

  // Last resort: generic open with the same attribute.
  rc = fsOpenSaveDataFileSystem(fs, space, &attr);
  if (R_SUCCEEDED(rc)) {
    printf("opened: generic\n");
    return rc;
  }
  printf("attempt3 rc=0x%x\n", rc);

  // Also try generic open with the reader's original (app=0) attrs.
  attr.application_id = info->application_id;
  rc = fsOpenSaveDataFileSystem(fs, space, &attr);
  if (R_SUCCEEDED(rc)) printf("opened: generic exact\n");
  else printf("attempt4 rc=0x%x\n", rc);
  return rc;
}

static void dumpEntry(FsFileSystem *fs) {
  u8 buf[BUF_SIZE];
  u64 read = 0;
  FsFile file;
  // FsOpenMode_Read == BIT(0); named identically on libnx and libounce.
  Result rc = fsFsOpenFile(fs, ENTRY_PATH, FsOpenMode_Read, &file);
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
  printf("last 'no upd': %llu (%s)\n", (unsigned long long)last_no_update,
         t2);
}

int main(int argc, char *argv[]) {
  consoleInit(NULL);

  FsSaveDataInfo info;
  memset(&info, 0, sizeof(info));
  FsFileSystem fs;
  Result rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);

  if (findSsVersion(&info)) {
    rc = openFromInfo(&fs, &info);
    if (R_SUCCEEDED(rc)) {
      dumpEntry(&fs);
      fsFsClose(&fs);
    } else {
      printf("open failed: 0x%x\n", rc);
    }
  } else {
    printf("no SystemSaveData matched\n");
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
