// vslist: dump ns's VersionList - the per-title table of "latest known
// update version" that ns:am consults for the launch-time update
// dialog. Pure IPC, ns stays alive. v1.1 tries cmd 801 ListVersionList
// (sync entry array) first, falls back to cmd 802 RequestVersionListData
// (async blob) with stride autodetect. Installed-title section kept as
// section 2. A=clear update-request flags, Plus=exit. Builds for
// Switch 1 (libnx) and Switch 2 (libounce) from this one file.

#include <stdio.h>
#include <string.h>

#include <switch.h>

// nn::cm DisplayVersion packing: a.b.c.d out of one u32.
static void fmtVer(char *out, size_t n, u32 v) {
  u32 a = (v >> 26) & 0x3FFF, b = (v >> 20) & 0x3F;
  u32 c = (v >> 14) & 0x3F, d = v & 0x3FFF;
  snprintf(out, n, "%u.%u.%u.%u", a, b, c, d);
}

static u64 ld64(const u8 *p) {
  return (u64)p[0] | ((u64)p[1] << 8) | ((u64)p[2] << 16) | ((u64)p[3] << 24) |
         ((u64)p[4] << 32) | ((u64)p[5] << 40) | ((u64)p[6] << 48) |
         ((u64)p[7] << 56);
}
static u32 ld32(const u8 *p) {
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static bool tidOk(u64 t) {
  u32 hi = (u32)(t >> 32);
  return hi >= 0x01000000 && hi <= 0x01FFFFFF;
}
static bool verOk(u32 v) {
  u32 a = (v >> 26) & 0x3FFF, b = (v >> 20) & 0x3F, c = (v >> 14) & 0x3F;
  return a && a < 200 && b < 100 && c < 100;
}

static void hexHead(const u8 *buf, u64 size, u64 maxb) {
  u64 n = size < maxb ? size : maxb;
  printf("-- raw --\n");
  for (u64 i = 0; i < n; i += 2) {
    printf("%02x%02x%s", buf[i], buf[i + 1 < n ? i + 1 : i],
           (((i / 2) % 8) == 7 || i + 2 >= n) ? "\n" : " ");
  }
}

// One stride candidate. voff = where the DisplayVersion sits in an
// entry. Prints entries on success. Returns true if layout accepted.
static bool tryStride(const u8 *buf, u64 size, u64 count, u64 stride,
                      u64 voff) {
  if (count == 0 || count > 512) return false;
  if (count * stride > size) return false;
  u64 chk = count < 4 ? count : 4;
  for (u64 i = 0; i < chk; i++) {
    u64 off = i * stride;
    if (off + 12 > size) return false;
    if (!tidOk(ld64(buf + off))) return false;
    if (!verOk(ld32(buf + off + voff))) return false;
  }
  for (u64 i = 0; i < count; i++) {
    u64 off = i * stride;
    char ver[24];
    fmtVer(ver, sizeof(ver), ld32(buf + off + voff));
    printf("%016llx  v%s (0x%08x)\n", (unsigned long long)ld64(buf + off),
           ver, ld32(buf + off + voff));
  }
  return true;
}

// Try all plausible layouts over a blob with an explicit entry count.
// Returns number of lines printed (entries), 0 = need hexdump.
static u64 decodeEntries(const u8 *buf, u64 size, u64 count) {
  static const u64 strides[] = {16, 24, 32, 12, 8};
  printf("-- version list (%llu entries) --\n", count);
  for (unsigned s = 0; s < sizeof(strides) / sizeof(strides[0]); s++)
    if (tryStride(buf, size, count, strides[s], 8)) return count;
  return 0;
}

// Blob with unknown count: header u32 at offset 0 or 4, entries right
// after the header. Same stride sweep. Falls back to hexdump.
static void decodeBlob(const u8 *buf, u64 size) {
  u32 cnt = ld32(buf);
  static const u64 hdrs[] = {4, 8};
  if (cnt > 0 && cnt <= 512) {
    for (unsigned h = 0; h < 2; h++)
      if (decodeEntries(buf + hdrs[h], size - hdrs[h], cnt)) return;
  }
  printf("-- blob: hdr?=%u size=%llu --\n", cnt, size);
  hexHead(buf, size, 256);
}

static Result getListService(Service *srv) {
  memset(srv, 0, sizeof(*srv));
  if (hosversionAtLeast(3, 0, 0))
    return nsGetApplicationManagerInterface(srv);
  Service *p = nsGetServiceSession_ApplicationManagerInterface();
  if (p) { *srv = *p; return 0; }
  return MAKERESULT(Module_Libnx, LibnxError_NotFound);
}

// cmd 801 ListVersionList: out buffer = entry array, s32 entrycount.
static void dumpVersionList(Service *srv) {
  static u8 buf[0x4000];
  s32 got = 0;
  Result rc = serviceDispatchImpl(
      srv, 801, NULL, 0, &got, sizeof(got),
      (SfDispatchParams){
          .buffer_attrs = { .attr0 = SfBufferAttr_HipcAutoSelect |
                                    SfBufferAttr_Out },
          .buffers = { { .ptr = buf, .size = sizeof(buf) } },
      });
  printf("ListVersionList: rc=0x%x entries=%d\n", rc, got);
  if (R_FAILED(rc) || got <= 0) return;
  if (decodeEntries(buf, sizeof(buf), (u64)got)) return;
  hexHead(buf, sizeof(buf), 256);
}

// cmd 802 RequestVersionListData: Event + IAsyncValue holding the blob.
static void dumpVersionListAsync(Service *srv) {
  Service av; memset(&av, 0, sizeof(av));
  Event ev; memset(&ev, 0, sizeof(ev));
  Handle eh = INVALID_HANDLE;

  Result rc = serviceDispatchImpl(
      srv, 802, NULL, 0, NULL, 0,
      (SfDispatchParams){
          .out_num_objects = 1, .out_objects = &av,
          .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
          .out_handles = &eh,
      });
  printf("RequestVersionListData: rc=0x%x\n", rc);
  if (R_FAILED(rc)) return;

  eventLoadRemote(&ev, eh, false);
  AsyncValue a = { .s = av, .event = ev };
  rc = asyncValueWait(&a, UINT64_MAX);
  if (R_FAILED(rc)) { printf("wait:0x%x\n", rc); asyncValueClose(&a); return; }

  u64 size = 0;
  rc = asyncValueGetSize(&a, &size);
  printf("value size=%llu rc=0x%x\n", size, rc);
  static u8 buf[0x4000];
  if (R_SUCCEEDED(rc) && size > 0 && size <= sizeof(buf)) {
    rc = asyncValueGet(&a, buf, size);
    if (R_SUCCEEDED(rc)) decodeBlob(buf, size);
    else printf("get:0x%x\n", rc);
  }
  asyncValueClose(&a);
}

// Installed titles with ns' queued-update marker (was v1.0's main view).
static void dumpInstalled(void) {
  printf("\n-- installed --\n");
  NsApplicationRecord recs[0x20];
  s32 off = 0, total = 0;
  while (1) {
    s32 got = 0;
    Result rc = nsListApplicationRecord(recs, 0x20, off, &got);
    if (R_FAILED(rc) || got <= 0) break;
    for (s32 i = 0; i < got; i++) {
      bool req = false;
      u32 fl = 0;
      nsIsApplicationUpdateRequested(recs[i].application_id, &req, &fl);
      printf("%016llx%s\n", (unsigned long long)recs[i].application_id,
             req ? "  UPD-REQ" : "");
      total++;
    }
    off += got;
    if (got < 0x20) break;
  }
  printf("%d titles\n", total);
}

static void withdrawAll(void) {
  NsApplicationRecord recs[0x20];
  s32 off = 0;
  while (1) {
    s32 got = 0;
    if (R_FAILED(nsListApplicationRecord(recs, 0x20, off, &got)) || got <= 0)
      break;
    for (s32 i = 0; i < got; i++)
      nsWithdrawApplicationUpdateRequest(recs[i].application_id);
    off += got;
    if (got < 0x20) break;
  }
}

int main(int argc, char *argv[]) {
  consoleInit(NULL);
  Result rc = nsInitialize();
  printf("vslist v1.1  hos=0x%x  ns:0x%x\n", hosversionGet(), rc);

  Service am;
  Result arc = getListService(&am);
  printf("ns:am rc=0x%x\n\n", arc);
  if (R_SUCCEEDED(arc)) {
    dumpVersionList(&am);
    dumpVersionListAsync(&am);
    serviceClose(&am);
  }

  PadState pad;
  padInitializeDefault(&pad);
  bool first = true;
  while (appletMainLoop()) {
    if (first) {
      dumpInstalled();
      printf("\nA=clear UPD-REQ   Plus=exit\n");
      first = false;
    }
    padUpdate(&pad);
    u64 down = padGetButtonsDown(&pad);
    if (down & HidNpadButton_Plus) break;
    if (down & HidNpadButton_A) { withdrawAll(); printf("cleared\n"); }
    consoleUpdate(NULL);
  }
  nsExit();
  consoleExit(NULL);
  return 0;
}
