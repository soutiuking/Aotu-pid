#ifndef __OLEDFONT_H
#define __OLEDFONT_H

/* 字库数据: 定义在 oledfont.c, 此处仅作 extern 声明.
 * 切勿在本头文件中直接定义数组, 否则多个 .c 包含后会触发链接器 L6200E 重复定义错误. */

/* 6*8 ASCII字符集点阵 */
extern const unsigned char asc2_0806[][6];
/* 12*12 ASCII字符集点阵 */
extern const unsigned char asc2_1206[95][12];
/* 16*16 ASCII字符集点阵 */
extern const unsigned char asc2_1608[][16];
/* 24*24 ASCII字符集点阵 */
extern const unsigned char asc2_2412[][36];
/* 汉字点阵 (16x16 / 16x24 / 24x24 / 32x32) */
extern const unsigned char Hzk1[][32];
extern const unsigned char Hzk2[][72];
extern const unsigned char Hzk3[][128];
extern const unsigned char Hzk4[][512];

#endif
