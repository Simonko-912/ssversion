// ssversion: dump the SystemUpdate ssversion SystemSaveData (id 0x49 /
// 0x8000000000000049, ns_ssversion:/entry) as hex + parsed fields.
// Builds for Switch 1 (libnx) and Switch 2 (libounce) from this one file.
//
// Strategy (v1.4, modelled on Adubbz/Gag-Order which reads the same save):
// enumerate with the save-info reader to find the record, then mount it
// with fsdevMountSystemSaveData() - that helper assembles the attribute
// set the way fsp-srv expects (ssid keeps the 0x8... high bits) instead
// of hand-building it, which is why plain fsOpen...BySystemSaveDataId
// calls reject with Fs errors (0xe02 etc). Retry rounds of the mount like
// Gag-Order does; fall back to direct opens if the helper is unavailable.

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#define ENTRY_PATH "/entry"
#define BUF_SIZE 0x40
#define DEV_NAME "ssv"

// Owner of the ssversion SystemSaveData: SystemUpdate (ns).
#define OWNER_TID 0x0100000000000816ULL

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

static FsFileSystem s_fs; // survives for direct-open handles

// Gag-Order style: fsdevMountSystemSaveData + fsdevGetDeviceFileSystem.
// Retry rounds in case ns is mid-write; several spaces/ssid/uid variants.
static FsFileSystem *mountViaHelper(const FsSaveDataInfo *info) {
  AccountUid zero;
  memset(&zero, 0, sizeof(zero));

  FsSaveDataSpaceId spaces[2] = { (FsSaveDataSpaceId)info->save_data_space_id,
                                  FsSaveDataSpaceId_ProperSystem };
  u64 ssids[2] = { info->system_save_data_id, 0x49ULL };
  const AccountUid *uids[2] = { &info->uid, &zero };

  Result rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);
  for (int round = 0; round < 20 && R_FAILED(rc); round++) {
    for (int u = 0; u < 2 && R_FAILED(rc); u++) {
      for (int s = 0; s < 2 && R_FAILED(rc); s++) {
        for (int i = 0; i < 2 && R_FAILED(rc); i++) {
          rc = fsdevMountSystemSaveData(DEV_NAME, spaces[s], ssids[i], *uids[u]);
          if (R_FAILED(rc))
            printf("mount sp=%d ssid=%s uid=%s -> 0x%x\n", (int)spaces[s],
                   ssids[i] == 0x49ULL ? "49" : "raw",
                   u == 0 ? "rec" : "zero", rc);
        }
      }
    }
  }

  if (R_FAILED(rc)) return NULL;
  printf("mounted via fsdevMountSystemSaveData\n");
  return fsdevGetDeviceFileSystem(DEV_NAME);
}

// Direct-open fallback chain: exact enumerated attrs, then + owner tid.
static FsFileSystem *openDirect(const FsSaveDataInfo *info) {
  FsSaveDataAttribute attr;
  memset(&attr, 0, sizeof(attr));
  attr.application_id = info->application_id;
  attr.uid = info->uid;
  attr.system_save_data_id = info->system_save_data_id;
  attr.save_data_type = info->save_data_type;
  attr.save_data_rank = info->save_data_rank;
  attr.save_data_index = info->save_data_index;

  FsSaveDataSpaceId space = (FsSaveDataSpaceId)info->save_data_space_id;
  Result rc = fsOpenSaveDataFileSystemBySystemSaveDataId(&s_fs, space, &attr);
  if (R_SUCCEEDED(rc)) {
    printf("opened: bySsid exact\n");
    return &s_fs;
  }
  printf("bySsid exact rc=0x%x\n", rc);

  attr.application_id = OWNER_TID;
  rc = fsOpenSaveDataFileSystemBySystemSaveDataId(&s_fs, space, &attr);
  if (R_SUCCEEDED(rc)) printf("opened: bySsid owner-tid\n");
  else printf("bySsid owner rc=0x%x\n", rc);
  return R_SUCCEEDED(rc) ? &s_fs : NULL;
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

  printf("ssversion dumper v1.4\n\n");

  FsFileSystem *fs = NULL;
  bool via_fsdev = false;
  if (findSsVersion(&info)) {
    printf("\npicked: space=%d ssid=%016llx app=%016llx type=%d rank=%d\n",
           (int)info.save_data_space_id,
           (unsigned long long)info.system_save_data_id,
           (unsigned long long)info.application_id,
           (int)info.save_data_type, (int)info.save_data_rank);
    fs = mountViaHelper(&info);
    if (fs) {
      via_fsdev = true;
    } else {
      fs = openDirect(&info);
    }
  } else {
    printf("no SystemSaveData matched\n");
  }

  if (fs) dumpEntry(fs);
  else printf("open failed\n");

  PadState pad;
  padInitializeDefault(&pad);
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    consoleUpdate(NULL);
  }
  if (via_fsdev) fsdevUnmountDevice(DEV_NAME);
  consoleExit(NULL);
  return 0;
}
