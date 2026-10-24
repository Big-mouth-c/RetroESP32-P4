/*
 * cnfont — 中文（CJK）点阵字体组件
 *
 * 使用预生成的 1bpp 点阵字库（由 tools/make_cnfont.py 从 TTF 生成，
 * GBK 全部约 2.1 万汉字，文件仅约 2.6MB，放 SD 卡）。
 *
 * 内存策略（关键）：
 *   - 不加载整个字库文件！文件句柄常驻，显示某字时才 fseek+fread
 *     该字的 128 字节点阵；
 *   - PSRAM 只占码点索引（约 84KB）；另有 64 槽直接映射点阵缓存
 *     （静态内嵌，约 9KB 内部 SRAM）；
 *   - 对模拟器内存几乎无影响。
 *
 * 用途：launcher 浏览器中显示中文 ROM 文件名（FATFS UTF-8）。
 * 若初始化失败（字库文件不存在等），cnfont_is_ready() 返回 false，
 * 调用方应回退到原有 ASCII / '?' 渲染路径。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 打开并初始化中文点阵字库
 *
 * @param font_path   字库路径，例如 "/sd/fonts/cnfont32.bin"
 * @param box_height  期望行框高度（像素），须与字库单元一致（32）
 * @return ESP_OK 成功；其余为失败（文件不存在 / 格式不符）
 */
esp_err_t cnfont_init(const char *font_path, int box_height);

/** 字库是否已就绪可用 */
bool cnfont_is_ready(void);

/**
 * @brief 解码一个 UTF-8 字符
 *
 * @param s   字符串起始
 * @param cp  [out] Unicode 码点
 * @return 消耗的字节数（1..4）；0 表示非法序列
 */
int cnfont_utf8_decode(const char *s, uint32_t *cp);

/** 该码点是否为非 ASCII 宽字符（需走点阵字库） */
bool cnfont_is_wide(uint32_t cp);

/**
 * @brief 获取字形（1bpp 点阵，带直接映射缓存）
 *
 * 所有偏移均相对于行框左上角（x 向右、y 向下）。
 *
 * @param cp  Unicode 码点
 * @return 字形指针；字库未就绪、码点无字形时返回 NULL。
 *         返回的指针仅在下一次 cnfont_get_glyph() 前有效。
 */
typedef struct {
    uint32_t cp;          /**< Unicode 码点 */
    int w;                /**< 位图宽（=单元 32） */
    int h;                /**< 位图高（=单元 32） */
    int xoff;             /**< 位图相对绘制原点 x（0） */
    int yoff;             /**< 位图相对绘制原点 y（0） */
    int advance;          /**< 水平步进（像素=单元 32） */
    const uint8_t *data;  /**< 1bpp 点阵：stride*rows 字节，MSB first */
} cnfont_glyph_t;

const cnfont_glyph_t *cnfont_get_glyph(uint32_t cp);

/** 关闭字库（一般无需调用） */
void cnfont_deinit(void);

#ifdef __cplusplus
}
#endif
