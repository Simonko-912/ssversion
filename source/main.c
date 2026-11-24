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

// Enumerate save records only as a fallback when the direct mount with
// the documented fixed id fails: find the exact stored attrs. Fill
// 'picked'. Returns false if no SystemSaveData matched.
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

  u64 oldPid = 0;
  pmshellGetProcessId(&oldPid, NS_TID);

  rc = pmshellTerminateProgram(NS_TID);
  printf("ns terminate: 0x%x\n", rc);

  // Hardware showed pm does NOT reliably relaunch ns on its own after
  // terminate (black screen until reboot). So don't depend on pm:
  // check briefly whether ns returned by itself, then explicitly
  // relaunch it ourselves via pmshellLaunchProgram, then verify.
  bool ready = false;
  u64 pid = 0;

  // Short grace so a still-registered ns isn't double-launched.
  for (int i = 0; i < 10 && !ready; i++) {
    svcSleepThread(100e6);
    pid = 0;
    if (R_SUCCEEDED(pmshellGetProcessId(&pid, NS_TID)) && pid != 0 &&
        pid != oldPid) ready = true;
  }

  if (!ready) {
    // ns lives in BuiltInSystem; storage 'Any' may not resolve for a
    // sysmodule, so try BuiltInSystem first, then Any, then 0.
    static const u8 storages[3] = { NcmStorageId_BuiltInSystem,
                                    NcmStorageId_Any,
                                    NcmStorageId_None };
    for (int a = 0; a < 3 && !ready; a++) {
      NcmProgramLocation loc;
      memset(&loc, 0, sizeof(loc));
      loc.program_id = NS_TID;
      loc.storageID = storages[a];
      u64 newPid = 0;
      Result lrc = pmshellLaunchProgram(PmLaunchFlag_None, &loc, &newPid);
      printf("ns launch st=%d: 0x%x\n", (int)storages[a], lrc);
      if (R_FAILED(lrc)) continue;
      // Verify: pid change via pm info, or ns service connectable.
      for (int i = 0; i < 30 && !ready; i++) {
        svcSleepThread(100e6);
        pid = 0;
        if (R_SUCCEEDED(pmdmntGetProcessId(&pid, NS_TID)) && pid != 0 &&
            pid != oldPid) {
          ready = true;
          break;
        }
        Result nrc = nsInitialize();
        if (R_SUCCEEDED(nrc)) { nsExit(); ready = true; }
      }
      if (pid == 0 && newPid != 0) pid = newPid;
    }
  }
  printf("ns back: %s pid=%llu\n", ready ? "yes" : "no",
         (unsigned long long)pid);
  pmshellExit();
}

// Gag-Order order: kill ns so it releases the save, then mount via the
// fsdev helper with the documented fixed id (switchbrew SystemSaveData:
// 0x8000000000000049 on both console generations). Zero uid, sweep
// sys/proper space + raw/trimmed id. Enumeration only runs if this all
// fails; loop rounds in case ns respawns and relocks mid-way.
static FsFileSystem *mountViaHelper(FsSaveDataInfo *info) {
  AccountUid zero;
  memset(&zero, 0, sizeof(zero));

  FsSaveDataSpaceId spaces[2] = { FsSaveDataSpaceId_System,
                                  FsSaveDataSpaceId_ProperSystem };
  u64 ssids[2] = { 0x8000000000000049ULL, 0x49ULL };

  Result rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);
  Result last = rc;

  // Round 1: mount with ns left running - reading the save usually
  // works and avoids the HOME-button crash seen after terminating ns.
  for (int s = 0; s < 2 && R_FAILED(rc); s++) {
    for (int i = 0; i < 2 && R_FAILED(rc); i++) {
      rc = fsdevMountSystemSaveData(DEV_NAME, spaces[s], ssids[i], zero);
      last = rc;
    }
  }

  // Round 2: enumerate to recover the exact stored attrs (esp. the
  // real uid instead of zero) and retry the helper mount before
  // touching ns at all - most HOME-crash reports came from the
  // terminate, so exhaust the gentle paths first.
  if (R_FAILED(rc) && findSsVersion(info)) {
    rc = fsdevMountSystemSaveData(DEV_NAME,
                                  (FsSaveDataSpaceId)info->save_data_space_id,
                                  info->system_save_data_id, info->uid);
    last = rc;
    if (R_FAILED(rc)) {
      rc = fsdevMountSystemSaveData(DEV_NAME,
                                    (FsSaveDataSpaceId)info->save_data_space_id,
                                    info->system_save_data_id, zero);
      last = rc;
    }
  }

  // Still locked? fall back to gag-order's order: kill ns, retry.
  for (int round = 0; round < 10 && R_FAILED(rc); round++) {
    terminateNs();
    for (int s = 0; s < 2 && R_FAILED(rc); s++) {
      for (int i = 0; i < 2 && R_FAILED(rc); i++) {
        rc = fsdevMountSystemSaveData(DEV_NAME, spaces[s], ssids[i], zero);
        last = rc;
      }
    }
    if (R_FAILED(rc)) {
      // Once more with the stored attrs from the reader (exact uid).
    if (info->system_save_data_id == 0) findSsVersion(info);
      if (!findSsVersion(info)) continue;
      rc = fsdevMountSystemSaveData(DEV_NAME,
                                    (FsSaveDataSpaceId)info->save_data_space_id,
                                    info->system_save_data_id, info->uid);
      last = rc;
      if (R_SUCCEEDED(rc)) break;
      rc = fsdevMountSystemSaveData(DEV_NAME,
                                    (FsSaveDataSpaceId)info->save_data_space_id,
                                    info->system_save_data_id, zero);
      last = rc;
    }
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

  // Grouped hex: two-byte groups, 8 groups (16 bytes) per line:
  //   1608 0000 0000 0001 e001 5058 0300 0000
  printf("\n== hex (%llu bytes) ==\n", (unsigned long long)read);
  for (u64 i = 0; i < read; i += 2) {
    printf("%02x%02x%s", buf[i], buf[i + 1 < read ? i + 1 : i],
           (((i / 2) % 8) == 7 || i + 2 >= read) ? "\n" : " ");
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
  // supernag = two u16 tunables ns checks together: cycle minutes +
  // grace minutes. Reference pairs: nagged 0x14020028 = 40 / 5122,
  // gag-order's unnagged 0x100000C8 = 200 / 4096.
  printf("supernag     : 0x%08x -> %u / %u (two 16-bit halves)\n", supernag,
         (unsigned)(supernag & 0xFFFF), (unsigned)(supernag >> 16));
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

  printf("ssversion dumper v2.2\n\n");

  FsFileSystem *fs = NULL;
  bool via_fsdev = false;
  fs = mountViaHelper(&info);
  if (fs) {
    via_fsdev = true;
  } else {
    if (info.system_save_data_id == 0) findSsVersion(&info);
    if (info.system_save_data_id != 0) fs = openDirect(&info);
    else printf("no SystemSaveData matched\n");
  }

  if (fs) dumpEntry(fs);
  else printf("open failed\n");

  // Release the save before waiting on the user: gag-order order,
  // commit+unmount first, idle loop after. Keeps HOME-exit clean.
  if (via_fsdev) {
    fsdevUnmountDevice(DEV_NAME);
    via_fsdev = false;
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
