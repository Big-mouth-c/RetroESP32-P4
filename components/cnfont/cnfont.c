/*
 * cnfont — 中文点阵字体（实现）
 *
 * 预生成 1bpp 字库（tools/make_cnfont.py）：
 *   header(14B) + 码点表(4*count) + 点阵(128B*count)
 *
 * 运行时只把码点表读入 PSRAM（约 84KB），字形点阵按需从 SD 卡
 * 单字读取（128B/字），64 槽直接映射缓存内嵌在静态存储中。
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "cnfont.h"

static const char *TAG = "cnfont";

/* ========================= 字库格式 ========================= */

#define CNFONT_MAGIC "CNFB"
#define CNFONT_VERSION 1
#define CNFONT_HEADER_SIZE 14   /* magic4 + ver2 + unit/stride/rows/rsv 4 + count4 */

/* ========================= 内部状态 ========================= */

static FILE *s_fp = NULL;
static uint32_t *s_cps = NULL;   /* 升序码点表（PSRAM） */
static uint32_t s_count = 0;
static uint32_t s_bitmap_base = 0;
static int s_unit = 0;           /* 格子边长 */
static int s_stride = 0;         /* 每行字节数 */
static int s_rows = 0;
static int s_glyph_bytes = 0;    /* 每字点阵字节数 */
static bool s_ready = false;

/* ========================= 点阵缓存（直接映射，静态内嵌） ========================= */

#define CNFONT_CACHE_SLOTS 64

typedef struct {
    uint32_t cp;
    bool used;
    cnfont_glyph_t glyph;
    uint8_t data[128];
} cache_slot_t;

static cache_slot_t s_cache[CNFONT_CACHE_SLOTS];

/* ========================= 公共 API ========================= */

esp_err_t cnfont_init(const char *font_path, int box_height)
{
    if (s_ready) {
        return ESP_OK;
    }
    if (!font_path || box_height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(font_path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "无法打开点阵字库: %s", font_path);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t hdr[CNFONT_HEADER_SIZE];
    if (fread(hdr, 1, CNFONT_HEADER_SIZE, f) != CNFONT_HEADER_SIZE ||
        memcmp(hdr, CNFONT_MAGIC, 4) != 0) {
        ESP_LOGE(TAG, "字库格式无效（magic 不符）: %s", font_path);
        fclose(f);
        return ESP_ERR_INVALID_VERSION;
    }

    uint16_t version = (uint16_t)(hdr[4] | (hdr[5] << 8));
    int unit   = hdr[6];
    int stride = hdr[7];
    int rows   = hdr[8];
    uint32_t count = (uint32_t)hdr[10] | ((uint32_t)hdr[11] << 8)
                   | ((uint32_t)hdr[12] << 16) | ((uint32_t)hdr[13] << 24);

    if (version != CNFONT_VERSION || unit != box_height ||
        stride <= 0 || rows <= 0 || count == 0) {
        ESP_LOGE(TAG, "字库版本/尺寸不符: ver=%u unit=%d(期望%d) count=%lu",
                 version, unit, box_height, (unsigned long)count);
        fclose(f);
        return ESP_ERR_INVALID_VERSION;
    }

    uint32_t *cps = (uint32_t *)heap_caps_malloc((size_t)count * 4, MALLOC_CAP_SPIRAM);
    if (!cps) {
        /* PSRAM 不可用时回退普通堆（约 84KB） */
        cps = (uint32_t *)malloc((size_t)count * 4);
    }
    if (!cps) {
        ESP_LOGE(TAG, "码点表内存分配失败（%lu 字节）", (unsigned long)(count * 4));
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    if (fread(cps, 4, count, f) != count) {
        ESP_LOGE(TAG, "码点表读取不完整");
        heap_caps_free(cps);
        fclose(f);
        return ESP_ERR_INVALID_VERSION;
    }

    s_fp = f;
    s_cps = cps;
    s_count = count;
    s_unit = unit;
    s_stride = stride;
    s_rows = rows;
    s_glyph_bytes = stride * rows;
    s_bitmap_base = CNFONT_HEADER_SIZE + count * 4;
    memset(s_cache, 0, sizeof(s_cache));
    s_ready = true;

    ESP_LOGI(TAG, "中文字库就绪: %s（%lu 字, 单元 %d, PSRAM 索引约 %lu KB）",
             font_path, (unsigned long)count, unit,
             (unsigned long)((count * 4) / 1024));
    return ESP_OK;
}

bool cnfont_is_ready(void)
{
    return s_ready;
}

int cnfont_utf8_decode(const char *s, uint32_t *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char c = p[0];

    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        if ((p[1] & 0xC0) != 0x80) return 0;
        *cp = ((uint32_t)(c & 0x1F) << 6) | (p[1] & 0x3F);
        return (*cp >= 0x80) ? 2 : 0;
    }
    if ((c & 0xF0) == 0xE0) {
        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return 0;
        *cp = ((uint32_t)(c & 0x0F) << 12)
            | ((uint32_t)(p[1] & 0x3F) << 6)
            | (p[2] & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 ||
            (p[3] & 0xC0) != 0x80) return 0;
        *cp = ((uint32_t)(c & 0x07) << 18)
            | ((uint32_t)(p[1] & 0x3F) << 12)
            | ((uint32_t)(p[2] & 0x3F) << 6)
            | (p[3] & 0x3F);
        return (*cp <= 0x10FFFF && *cp >= 0x10000) ? 4 : 0;
    }
    return 0;
}

bool cnfont_is_wide(uint32_t cp)
{
    return cp >= 0x80;
}

const cnfont_glyph_t *cnfont_get_glyph(uint32_t cp)
{
    if (!s_ready) return NULL;

    int slot = (int)(cp % CNFONT_CACHE_SLOTS);
    cache_slot_t *s = &s_cache[slot];
    if (s->used && s->cp == cp) {
        return &s->glyph;  /* 缓存命中 */
    }

    /* 二分查找码点表 */
    int lo = 0, hi = (int)s_count - 1, idx = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t mcp = s_cps[mid];
        if (mcp == cp) { idx = mid; break; }
        if (mcp < cp) lo = mid + 1; else hi = mid - 1;
    }
    if (idx < 0) {
        return NULL;  /* 字库不含该字 */
    }

    /* 从 SD 卡读取该字点阵（128 字节）到缓存槽 */
    long off = (long)(s_bitmap_base + (uint32_t)idx * (uint32_t)s_glyph_bytes);
    if (fseek(s_fp, off, SEEK_SET) != 0 ||
        fread(s->data, 1, s_glyph_bytes, s_fp) != (size_t)s_glyph_bytes) {
        ESP_LOGW(TAG, "字形读取失败 cp=0x%lX off=%ld", (unsigned long)cp, off);
        return NULL;
    }

    s->cp = cp;
    s->used = true;
    s->glyph.cp = cp;
    s->glyph.w = s_unit;
    s->glyph.h = s_rows;
    s->glyph.xoff = 0;
    s->glyph.yoff = 0;
    s->glyph.advance = s_unit;
    s->glyph.data = s->data;
    return &s->glyph;
}

void cnfont_deinit(void)
{
    if (!s_ready) return;
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    if (s_cps) {
        heap_caps_free(s_cps);
        s_cps = NULL;
    }
    memset(s_cache, 0, sizeof(s_cache));
    s_ready = false;
}
