// ssversion: dump the SystemUpdate ssversion SystemSaveData (id
// 0x8000000000000049 per switchbrew SystemSaveData, ns_ssversion:/entry)
// as hex + parsed fields. Builds for Switch 1 (libnx) and Switch 2
// (libounce) from this one file.
//
// Strategy (v1.5, follows Adubbz/Gag-Order's proven sequence): terminate
// ns via pmshell FIRST so it releases the save, then mount through the
// fsdev helper fsdevMountSystemSaveData() which assembles the attribute
// tuple fsp-srv wants. Direct bySsid opens stay as fallback. Console
// output is kept short so nothing important scrolls off.

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#define ENTRY_PATH "/entry"
#define BUF_SIZE 0x40
#define DEV_NAME "ssv"

// ns (SystemUpdate) program id, per Gag-Order's terminate call.
#define NS_TID 0x010000000000001FULL
// Owner of the ssversion save as stored in the entry header itself.
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

static void printRec(const char *tag, const FsSaveDataInfo *info) {
  printf("%s space=%d ssid=%016llx app=%016llx type=%d rank=%d\n", tag,
         (int)info->save_data_space_id,
         (unsigned long long)info->system_save_data_id,
         (unsigned long long)info->application_id,
         (int)info->save_data_type, (int)info->save_data_rank);
}

// Enumerate save records; print count + the matched record only, so the
// console keeps the interesting lines on screen. Fill 'picked'.
static bool findSsVersion(FsSaveDataInfo *picked) {
  FsSaveDataInfoReader reader;
  bool found = false;
  bool fallback_done = false;
  int count = 0;

  Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_All);
  if (R_FAILED(rc)) {
    printf("infoReader: 0x%x\n", rc);
    return false;
  }

  FsSaveDataInfo info;
  s64 total = 0;
  do {
    total = 0;
    rc = fsSaveDataInfoReaderRead(&reader, &info, 1, &total);
    if (R_FAILED(rc) || total == 0) break;
    count++;
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
  printf("scanned %d saves\n", count);
  if (found) printRec("match:", picked);
  else if (fallback_done) printRec("fallback:", picked);
  return found || fallback_done;
}

static void terminateNs(void) {
  Result rc = pmshellInitialize();
  if (R_FAILED(rc)) { printf("pmshell: 0x%x\n", rc); return; }
  rc = pmshellTerminateProgram(NS_TID);
  printf("ns terminate: 0x%x\n", rc);
  pmshellExit();
}

// Gag-Order order: kill ns so it releases the save, then mount via the
// fsdev helper. Sweep space/ssid/uid variants; loop rounds in case ns
// respawns and grabs the save again mid-way.
static FsFileSystem *mountViaHelper(const FsSaveDataInfo *info) {
  AccountUid zero;
  memset(&zero, 0, sizeof(zero));

  FsSaveDataSpaceId spaces[2] = { (FsSaveDataSpaceId)info->save_data_space_id,
                                  FsSaveDataSpaceId_ProperSystem };
  u64 ssids[2] = { info->system_save_data_id, 0x49ULL };
  const AccountUid *uids[2] = { &info->uid, &zero };

  Result rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);
  Result last = rc;
  terminateNs();
  for (int round = 0; round < 10 && R_FAILED(rc); round++) {
    for (int u = 0; u < 2 && R_FAILED(rc); u++) {
      for (int s = 0; s < 2 && R_FAILED(rc); s++) {
        for (int i = 0; i < 2 && R_FAILED(rc); i++) {
          rc = fsdevMountSystemSaveData(DEV_NAME, spaces[s], ssids[i], *uids[u]);
          last = rc;
        }
      }
    }
    if (R_FAILED(rc)) terminateNs(); // ns may have re-locked it
  }

  if (R_FAILED(rc)) {
    printf("helper mount failed: 0x%x\n", last);
    return NULL;
  }
  printf("mounted via helper\n");
  return fsdevGetDeviceFileSystem(DEV_NAME);
}

// Direct-open fallback: exact enumerated attrs, then + owner tid.
static FsFileSystem s_fs;

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
    printf("read " ENTRY_PATH " failed: 0x%x (read %llu)\n", rc,
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

  printf("ssversion dumper v1.5\n\n");

  FsFileSystem *fs = NULL;
  bool via_fsdev = false;
  if (findSsVersion(&info)) {
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
