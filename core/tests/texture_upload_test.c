// Unit test of the real upload/LOD state machine, with a fake GPU recorder.
// No Vulkan device, game, input replay or game assets are used.
// clang -std=gnu17 -O1 -ffunction-sections -fdata-sections -Icore/runtime \
//   -Icore/runtime/third_party/vma core/tests/texture_upload_test.c \
//   -Wl,--gc-sections -lpthread -lm -o /tmp/texture-upload-test
#include "../runtime/vk_texture.c"
#include <assert.h>

GfxStats gfx_counters;
unsigned rt_trace;
static VkCtx context = {.has_min_lod = 1};
static int decode_ok = 1, calls, begins, ends, perf_tex_ends, copies;
static VkImage fail_image;
static int change = 1;
static uint8_t staging[16384];

VkCtx *vk_ctx(void) { return &context; }
const char *cfg_get(const char *s) { return NULL; }
int cfg_bool(const char *s) { return 0; }   // no partial non-ASTC uploads in these fixtures
void rt_log(const char *s, ...) {}
void rt_fatal(const char *s, ...) { abort(); }
uint64_t kern_now_ns(void) { static uint64_t t; return ++t; }
uint64_t perf_begin(void) { return kern_now_ns(); }
void perf_end(int kind, uint64_t t) { if (kind == PERF_TEX) perf_tex_ends++; }
void perf_count(int kind) {}
int vk_astc_gpu(void) { return 1; }
int vk_astc_mode(void) { return VK_ASTC_GPU; }
VkCommandBuffer vk_upload_begin(void) { begins++; return (VkCommandBuffer)1; }
void vk_upload_end(void) { ends++; }
uint64_t vk_upload_flush(int wait) { return 0; }
void vk_upload_note(int kind, uint64_t n) {}
void *vk_staging(VkDeviceSize size, VkBuffer *buf, VmaAllocation *a) {
    assert(size <= sizeof staging); *buf = (VkBuffer)1; return staging;
}
int nvn_fmt_bpb(int format, int *bw, int *bh) { *bw = *bh = 4; return 16; }
int nvn_deswizzle(const nvn_texdesc *d, unsigned l, unsigned a, const void *s, void *out, size_t pitch) { return 0; }
int vk_astc_record(VkCommandBuffer c, VkBuffer s, VkDeviceSize sz, const VkAstcRegion *r, int n,
                   uint32_t bw, uint32_t bh, int srgb, int bc, VkImage img, VkImageAspectFlags aspect, VkBuffer rb) {
    calls++;
    return decode_ok && img != fail_image;
}
void vkCmdPipelineBarrier2(VkCommandBuffer c, const VkDependencyInfo *d) {}
void vkCmdCopyBufferToImage(VkCommandBuffer c, VkBuffer b, VkImage i, VkImageLayout l, uint32_t n,
                            const VkBufferImageCopy *r) { copies++; }
void vmaDestroyBuffer(VmaAllocator a, VkBuffer b, VmaAllocation m) { abort(); }
VkResult vmaInvalidateAllocation(VmaAllocator a, VmaAllocation m, VkDeviceSize off, VkDeviceSize size) { abort(); }
void astc_decode_ldr(const uint8_t *b, uint32_t w, uint32_t h, uint32_t bw, uint32_t bh, bool srgb, uint8_t *out) { abort(); }
void astc_decode_ldr_mt(const uint8_t *b, uint32_t w, uint32_t h, uint32_t bw, uint32_t bh, bool srgb, uint8_t *out) { abort(); }
int vk_mem_changed(uint64_t addr, uint64_t sz, uint32_t since, uint32_t *now) { *now = 9; return change; }
int vk_mem_changed_snap(uint64_t addr, uint64_t sz, uint32_t since, uint32_t epoch, uint32_t *now) {
    return vk_mem_changed(addr, sz, since, now);
}

static Tex texture(int id) {
    Tex e = {0};
    e.t.mem = staging;
    e.t.d.width = e.t.d.height = 16;
    e.t.d.target = NVN_TEXTURE_TARGET_2D;
    e.pub.image = (VkImage)(uintptr_t)id;
    e.pub.addr = id * 4096;
    e.pub.mirrored = 1;
    e.pub.type = VK_IMAGE_TYPE_2D;
    e.pub.levels = 3;
    e.pub.layers = 1;
    e.f.astc_bw = e.f.astc_bh = 4;
    return e;
}

int main(void) {
    Tex a = texture(1), b = texture(2);
    UpStat st;
    decode_ok = 0;
    assert(!upload_levels(&a, &st, 0, 3));
    assert(begins == ends && copies == 0);   // cleanup without copying raw ASTC into RGBA/BC3
    upload(&a);
    assert(a.upload_failed && !a.uploaded && a.n_up == 0 && perf_tex_ends == 1);
    uint32_t generation = slots_gen;
    decode_ok = 1;
    make_fresh_snap(&a);
    assert(a.uploaded && !a.upload_failed && a.n_up == 1 && slots_gen != generation);

    a.wgen = 4;
    decode_ok = 0;
    make_fresh_snap(&a);
    assert(a.upload_failed && a.uploaded && a.wgen == 4 && a.n_up == 1);
    change = 0;   // write-watch has consumed the original write
    decode_ok = 1;
    make_fresh_snap(&a);
    assert(!a.upload_failed && a.wgen == 9 && a.n_up == 2);
    change = 1;
    a.wgen = 4;
    decode_ok = 0;
    make_fresh(&a);
    assert(a.upload_failed && a.wgen == 4);
    change = 0;
    decode_ok = 1;
    make_fresh(&a);
    assert(!a.upload_failed && a.wgen == 9 && a.n_up == 3);

    Tex *queue[2] = {&a, &b};
    astc_pend = queue; nastc_pend = 2; castc_pend = 2;
    a.pub.min_lod = b.pub.min_lod = 2;
    a.astc_pending = b.astc_pending = 1;
    fail_image = a.pub.image;
    gfx_counters.frames++;
    int before = calls;
    astc_pend_step();
    assert(a.pub.min_lod == 2 && a.astc_pending && nastc_pend == 1);
    assert(b.pub.min_lod == 0 && !b.astc_pending);   // failure did not starve B
    assert(calls - before == 3);                   // A tried once; B uploaded two mips
    before = calls;
    astc_pend_step();
    assert(calls == before);                       // no retry in the same frame
    fail_image = VK_NULL_HANDLE;
    gfx_counters.frames++;
    astc_pend_step();
    assert(a.pub.min_lod == 0 && !a.astc_pending && nastc_pend == 0);
    queue[0] = &a; queue[1] = &b; nastc_pend = 2;
    a.pub.min_lod = b.pub.min_lod = 2;
    a.astc_pending = b.astc_pending = 1;
    decode_ok = 0;
    gfx_counters.frames++;
    before = calls;
    astc_pend_step();
    assert(calls - before == 2 && nastc_pend == 2);  // all failures are bounded, no spin
    assert(a.pub.min_lod == 2 && b.pub.min_lod == 2);
    assert(begins == ends);
    puts("PASS: decode failure, cleanup, first upload recovery, consumed-write retries, mip clamp, FIFO progress");
    return 0;
}
