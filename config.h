#ifndef KOE_CONFIG_H
#define KOE_CONFIG_H

#include <stdio.h>

#define KOE_PATH_MAX 1024
#define KOE_DEVICE_MAX 256

typedef struct {
    char core[KOE_PATH_MAX];      /* VOICEVOX CORE の配置先 */
    unsigned style;               /* スタイル ID */
    char device[KOE_DEVICE_MAX];  /* 出力デバイス ID。空なら OS 既定 */
    double volume;                /* 音量倍率 */
} koe_config;

/* 既定値を入れる。core の既定は data_home 配下の koe/voicevox_core。 */
void koe_config_default(koe_config *c, const char *data_home);

/* "key=value" 一行を反映する。空行・# 始まり・未知キーは無視して 0。
   値が不正なら -1。 */
int koe_config_parse_line(koe_config *c, const char *line);

/* ファイル全体を読む。全行が受理されれば 0、不正行があれば -1。 */
int koe_config_parse(koe_config *c, FILE *fp);

#endif
