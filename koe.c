/* koe: 日本語テキストを VOICEVOX CORE で合成し、miniaudio でその場で鳴らす。 */

#ifndef _WIN32
#define _DEFAULT_SOURCE /* setenv */
#endif
#define VOICEVOX_LOAD_ONNXRUNTIME
#include "voicevox_core.h"

#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_MP3
#define MA_NO_FLAC
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "config.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define LIB_NAME "voicevox_core.dll"
#define PATH_SEP "\\"
#else
#include <dirent.h>
#include <dlfcn.h>
#ifdef __APPLE__
#define LIB_NAME "libvoicevox_core.dylib"
#else
#define LIB_NAME "libvoicevox_core.so"
#endif
#define PATH_SEP "/"
#endif

enum { EXIT_FAIL = 1, EXIT_USAGE = 2 };

/* ---- 実行時に解決する VOICEVOX CORE の関数 ---- */

#define VV_FUNCS(X)                                             \
    X(voicevox_get_onnxruntime_lib_recommended_unversioned_filename) \
    X(voicevox_make_default_load_onnxruntime_options)           \
    X(voicevox_onnxruntime_load_once)                           \
    X(voicevox_open_jtalk_rc_new)                               \
    X(voicevox_open_jtalk_rc_delete)                            \
    X(voicevox_make_default_initialize_options)                 \
    X(voicevox_synthesizer_new)                                 \
    X(voicevox_synthesizer_delete)                              \
    X(voicevox_voice_model_file_open)                           \
    X(voicevox_voice_model_file_delete)                         \
    X(voicevox_make_default_load_voice_model_options)           \
    X(voicevox_synthesizer_load_voice_model)                    \
    X(voicevox_make_default_tts_options)                        \
    X(voicevox_synthesizer_tts)                                 \
    X(voicevox_synthesizer_create_audio_query)                  \
    X(voicevox_make_default_synthesis_options)                  \
    X(voicevox_synthesizer_synthesis)                           \
    X(voicevox_synthesizer_create_metas_json)                   \
    X(voicevox_json_free)                                       \
    X(voicevox_wav_free)                                        \
    X(voicevox_error_result_to_message)

#define VV_DECL(name) static __typeof__(name) *p_##name;
VV_FUNCS(VV_DECL)

static int load_core(const char *core) {
    /* core は tracing の EnvFilter を RUST_LOG で読む。未設定なら INFO が
       標準エラーに出るので、利用者が指定していない限り error に絞る。 */
#ifdef _WIN32
    if (!getenv("RUST_LOG")) _putenv_s("RUST_LOG", "error");
#else
    setenv("RUST_LOG", "error", 0);
#endif
    char path[KOE_PATH_MAX];
    snprintf(path, sizeof path, "%s" PATH_SEP "c_api" PATH_SEP "lib" PATH_SEP LIB_NAME, core);
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) { fprintf(stderr, "koe: cannot load %s\n", path); return -1; }
#define VV_LOAD(name) p_##name = (__typeof__(name) *)GetProcAddress(h, #name); \
    if (!p_##name) { fprintf(stderr, "koe: missing symbol %s\n", #name); return -1; }
#else
    void *h = dlopen(path, RTLD_NOW);
    if (!h) { fprintf(stderr, "koe: %s\n", dlerror()); return -1; }
#define VV_LOAD(name) p_##name = (__typeof__(name) *)dlsym(h, #name); \
    if (!p_##name) { fprintf(stderr, "koe: missing symbol %s\n", #name); return -1; }
#endif
    VV_FUNCS(VV_LOAD)
    return 0;
}

/* ---- ディレクトリ走査（辞書と音声モデルの発見） ---- */

typedef int (*entry_fn)(const char *dir, const char *name, void *ud);

static int for_each_entry(const char *dir, entry_fn fn, void *ud) {
#ifdef _WIN32
    char pattern[KOE_PATH_MAX];
    snprintf(pattern, sizeof pattern, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int rc = 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if ((rc = fn(dir, fd.cFileName, ud)) != 0) break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return rc;
#else
    DIR *d = opendir(dir);
    if (!d) return -1;
    int rc = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if ((rc = fn(dir, e->d_name, ud)) != 0) break;
    }
    closedir(d);
    return rc;
#endif
}

/* 名前が prefix で始まる最初のエントリのパスを out に入れる。 */
typedef struct { const char *prefix; char out[KOE_PATH_MAX]; } prefix_pick;

static int pick_prefix(const char *dir, const char *name, void *ud) {
    prefix_pick *p = ud;
    if (strncmp(name, p->prefix, strlen(p->prefix)) != 0) return 0;
    snprintf(p->out, sizeof p->out, "%s" PATH_SEP "%s", dir, name);
    return 1;
}

static int find_with_prefix(const char *dir, const char *prefix, char *out, size_t cap) {
    prefix_pick p = { prefix, "" };
    for_each_entry(dir, pick_prefix, &p);
    if (p.out[0] == '\0') {
        fprintf(stderr, "koe: no %s* under %s\n", prefix, dir);
        return -1;
    }
    snprintf(out, cap, "%s", p.out);
    return 0;
}

static int vv_fail(VoicevoxResultCode rc) {
    fprintf(stderr, "koe: %s\n", p_voicevox_error_result_to_message(rc));
    return -1;
}

static int load_vvm(const char *dir, const char *name, void *ud) {
    size_t n = strlen(name);
    if (n < 4 || strcmp(name + n - 4, ".vvm") != 0) return 0;
    char path[KOE_PATH_MAX];
    snprintf(path, sizeof path, "%s" PATH_SEP "%s", dir, name);
    struct VoicevoxVoiceModelFile *model;
    VoicevoxResultCode rc = p_voicevox_voice_model_file_open(path, &model);
    if (rc != VOICEVOX_RESULT_OK) return vv_fail(rc);
    rc = p_voicevox_synthesizer_load_voice_model((struct VoicevoxSynthesizer *)ud, model,
                                                 p_voicevox_make_default_load_voice_model_options());
    p_voicevox_voice_model_file_delete(model);
    if (rc != VOICEVOX_RESULT_OK) return vv_fail(rc);
    return 0;
}

/* 公式サンプル(example/cpp/unix/talk.cpp)と同じ順序で初期化する。 */
static struct VoicevoxSynthesizer *init_synthesizer(const char *core) {
    if (load_core(core) != 0) return NULL;

    /* Downloader が置く ONNX Runtime の版は core の「推奨」と一致しないことがあるので、
       版なしの名前を接頭辞にして実際にあるファイルを使う。 */
    char dir[KOE_PATH_MAX], path[KOE_PATH_MAX];
    struct VoicevoxLoadOnnxruntimeOptions ort = p_voicevox_make_default_load_onnxruntime_options();
    snprintf(dir, sizeof dir, "%s" PATH_SEP "onnxruntime" PATH_SEP "lib", core);
    if (find_with_prefix(dir, p_voicevox_get_onnxruntime_lib_recommended_unversioned_filename(), path, sizeof path) != 0) return NULL;
    ort.filename = path;
    const struct VoicevoxOnnxruntime *onnxruntime;
    VoicevoxResultCode rc = p_voicevox_onnxruntime_load_once(ort, &onnxruntime);
    if (rc != VOICEVOX_RESULT_OK) { vv_fail(rc); return NULL; }

    char dict[KOE_PATH_MAX];
    snprintf(dir, sizeof dir, "%s" PATH_SEP "dict", core);
    if (find_with_prefix(dir, "open_jtalk_dic", dict, sizeof dict) != 0) return NULL;
    struct OpenJtalkRc *open_jtalk;
    rc = p_voicevox_open_jtalk_rc_new(dict, &open_jtalk);
    if (rc != VOICEVOX_RESULT_OK) { vv_fail(rc); return NULL; }

    /* 0(自動)だと ONNX Runtime が CPU affinity を設定しようとして、cgroup 等で
       CPU が絞られた環境では失敗してログを吐き、遅くなる。明示すれば設定しない。
       合成時間は 2〜4 スレッドで頭打ちだった。 */
    struct VoicevoxInitializeOptions init = p_voicevox_make_default_initialize_options();
    init.cpu_num_threads = 4;
    struct VoicevoxSynthesizer *synth;
    rc = p_voicevox_synthesizer_new(onnxruntime, open_jtalk, init, &synth);
    p_voicevox_open_jtalk_rc_delete(open_jtalk);
    if (rc != VOICEVOX_RESULT_OK) { vv_fail(rc); return NULL; }

    snprintf(path, sizeof path, "%s" PATH_SEP "models" PATH_SEP "vvms", core);
    if (for_each_entry(path, load_vvm, synth) != 0) {
        fprintf(stderr, "koe: failed to load voice models from %s\n", path);
        p_voicevox_synthesizer_delete(synth);
        return NULL;
    }
    return synth;
}

/* ---- 再生 ---- */

/* バックエンドの識別子を設定ファイルに書ける文字列にする。 */
static void device_id_string(ma_backend backend, const ma_device_id *id, char *buf, size_t cap) {
    switch (backend) {
    case ma_backend_pulseaudio: snprintf(buf, cap, "%s", id->pulse); break;
    case ma_backend_alsa:       snprintf(buf, cap, "%s", id->alsa); break;
    case ma_backend_coreaudio:  snprintf(buf, cap, "%s", id->coreaudio); break;
#ifdef _WIN32
    case ma_backend_wasapi:
        /* wchar_t の ID を、UTF-8 ロケール(main で設定)の wcstombs で UTF-8 にする */
        if (wcstombs(buf, id->wasapi, cap) == (size_t)-1) buf[0] = '\0';
        buf[cap - 1] = '\0';
        break;
#endif
    default: snprintf(buf, cap, "(%s: unsupported backend)", ma_get_backend_name(backend)); break;
    }
}

static int list_devices(void) {
    ma_context ctx;
    if (ma_context_init(NULL, 0, NULL, &ctx) != MA_SUCCESS) {
        fprintf(stderr, "koe: audio context init failed\n");
        return EXIT_FAIL;
    }
    ma_device_info *infos;
    ma_uint32 count;
    if (ma_context_get_devices(&ctx, &infos, &count, NULL, NULL) != MA_SUCCESS) {
        fprintf(stderr, "koe: device enumeration failed\n");
        ma_context_uninit(&ctx);
        return EXIT_FAIL;
    }
    for (ma_uint32 i = 0; i < count; i++) {
        char id[KOE_DEVICE_MAX];
        device_id_string(ctx.backend, &infos[i].id, id, sizeof id);
        printf("%s%s\t%s\n", infos[i].isDefault ? "*" : "", id, infos[i].name);
    }
    ma_context_uninit(&ctx);
    return 0;
}

typedef struct {
    ma_decoder decoder;
    volatile int done;
} playback;

static void data_callback(ma_device *dev, void *out, const void *in, ma_uint32 frames) {
    (void)in;
    playback *p = dev->pUserData;
    ma_uint64 read = 0;
    ma_decoder_read_pcm_frames(&p->decoder, out, frames, &read);
    if (read < frames) p->done = 1;
}

static int find_device(ma_context *ctx, const char *wanted, ma_device_id *out) {
    ma_device_info *infos;
    ma_uint32 count;
    if (ma_context_get_devices(ctx, &infos, &count, NULL, NULL) != MA_SUCCESS) return -1;
    for (ma_uint32 i = 0; i < count; i++) {
        char id[KOE_DEVICE_MAX];
        device_id_string(ctx->backend, &infos[i].id, id, sizeof id);
        if (strcmp(id, wanted) == 0) { *out = infos[i].id; return 0; }
    }
    fprintf(stderr, "koe: no output device with id %s (see `koe devices`)\n", wanted);
    return -1;
}

static int play_wav(const uint8_t *wav, size_t len, const koe_config *cfg) {
    playback p = {0};
    if (ma_decoder_init_memory(wav, len, NULL, &p.decoder) != MA_SUCCESS) {
        fprintf(stderr, "koe: wav decode failed\n");
        return -1;
    }
    ma_context ctx;
    if (ma_context_init(NULL, 0, NULL, &ctx) != MA_SUCCESS) {
        fprintf(stderr, "koe: audio context init failed\n");
        ma_decoder_uninit(&p.decoder);
        return -1;
    }
    ma_device_id id;
    ma_device_config dc = ma_device_config_init(ma_device_type_playback);
    if (cfg->device[0] != '\0') {
        if (find_device(&ctx, cfg->device, &id) != 0) { ma_context_uninit(&ctx); ma_decoder_uninit(&p.decoder); return -1; }
        dc.playback.pDeviceID = &id;
    }
    dc.playback.format = p.decoder.outputFormat;
    dc.playback.channels = p.decoder.outputChannels;
    dc.sampleRate = p.decoder.outputSampleRate;
    dc.dataCallback = data_callback;
    dc.pUserData = &p;

    ma_device dev;
    int rc = -1;
    if (ma_device_init(&ctx, &dc, &dev) != MA_SUCCESS) {
        fprintf(stderr, "koe: audio device init failed\n");
    } else {
        ma_device_set_master_volume(&dev, (float)cfg->volume);
        if (ma_device_start(&dev) != MA_SUCCESS) {
            fprintf(stderr, "koe: audio device start failed\n");
        } else {
            while (!p.done) ma_sleep(10);
            /* 最後のバッファが鳴り終わるまで一拍待つ */
            ma_sleep(50);
            rc = 0;
        }
        ma_device_uninit(&dev);
    }
    ma_context_uninit(&ctx);
    ma_decoder_uninit(&p.decoder);
    return rc;
}

/* ---- 入力と設定 ---- */

static char *read_stdin(void) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, stdin)) > 0) {
        len += n;
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
    }
    buf[len] = '\0';
    return buf;
}

static char *join_args(int argc, char **argv) {
    size_t len = 0;
    for (int i = 0; i < argc; i++) len += strlen(argv[i]) + 1;
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    buf[0] = '\0';
    for (int i = 0; i < argc; i++) {
        if (i) strcat(buf, " ");
        strcat(buf, argv[i]);
    }
    return buf;
}

static int is_blank(const char *s) {
    for (; *s; s++) if (!strchr(" \t\r\n", *s)) return 0;
    return 1;
}

static void env_or(const char *name, const char *fallback_home, const char *fallback_sub, char *buf, size_t cap) {
    const char *v = getenv(name);
    if (v && v[0]) snprintf(buf, cap, "%s", v);
    else snprintf(buf, cap, "%s" PATH_SEP "%s", fallback_home, fallback_sub);
}

static int load_config(koe_config *cfg) {
    char data_home[KOE_PATH_MAX], config_home[KOE_PATH_MAX], path[KOE_PATH_MAX + 32];
#ifdef _WIN32
    env_or("LOCALAPPDATA", "", "", data_home, sizeof data_home);
    env_or("APPDATA", "", "", config_home, sizeof config_home);
#else
    const char *home = getenv("HOME");
    if (!home) home = "";
    env_or("XDG_DATA_HOME", home, ".local/share", data_home, sizeof data_home);
    env_or("XDG_CONFIG_HOME", home, ".config", config_home, sizeof config_home);
#endif
    koe_config_default(cfg, data_home);
    snprintf(path, sizeof path, "%s" PATH_SEP "koe" PATH_SEP "config", config_home);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    int rc = koe_config_parse(cfg, fp);
    fclose(fp);
    if (rc != 0) fprintf(stderr, "koe: invalid line in %s\n", path);
    return rc;
}

/* AudioQuery の JSON の話速 (speedScale) を speed にした文字列を作る (malloc)。キーが無ければ NULL。 */
static char *with_speed(const char *json, double speed) {
    static const char *keys[] = {"\"speedScale\":", "\"speed_scale\":"};
    for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) {
        const char *p = strstr(json, keys[k]);
        if (!p) continue;
        const char *v = p + strlen(keys[k]);
        while (*v == ' ') v++;
        const char *end = v;
        while (*end && strchr("+-.0123456789eE", *end)) end++;
        char *out = malloc(strlen(json) + 64);
        if (!out) return NULL;
        sprintf(out, "%.*s%g%s", (int)(v - json), json, speed, end);
        return out;
    }
    return NULL;
}

/* text を合成する。speed が 1 でなければ AudioQuery の話速を変えてから合成する。 */
static VoicevoxResultCode synthesize(struct VoicevoxSynthesizer *synth, const char *text, unsigned style,
                                     double speed, uintptr_t *wav_len, uint8_t **wav) {
    if (speed == 1.0)
        return p_voicevox_synthesizer_tts(synth, text, style, p_voicevox_make_default_tts_options(), wav_len, wav);
    char *query = NULL;
    VoicevoxResultCode vrc = p_voicevox_synthesizer_create_audio_query(synth, text, style, &query);
    if (vrc != VOICEVOX_RESULT_OK) return vrc;
    char *fast = with_speed(query, speed);
    p_voicevox_json_free(query);
    if (!fast) { fputs("koe: no speedScale in the audio query\n", stderr); return VOICEVOX_RESULT_INVALID_AUDIO_QUERY_ERROR; }
    vrc = p_voicevox_synthesizer_synthesis(synth, fast, style, p_voicevox_make_default_synthesis_options(), wav_len, wav);
    free(fast);
    return vrc;
}

/* 合成した WAV をそのままファイルに書く。 */
static int write_wav(const char *path, const uint8_t *wav, uintptr_t len) {
    FILE *fp = fopen(path, "wb");
    if (!fp) { fprintf(stderr, "koe: cannot open %s\n", path); return -1; }
    int ok = fwrite(wav, 1, len, fp) == len;
    if (fclose(fp) != 0) ok = 0;
    if (!ok) fprintf(stderr, "koe: cannot write %s\n", path);
    return ok ? 0 : -1;
}

static int usage(void) {
    fputs("usage: koe TEXT...\n"
          "       echo TEXT | koe\n"
          "       koe -o FILE TEXT...   write the WAV to FILE instead of playing\n"
          "       koe -s SPEED TEXT...  speak at SPEED (VOICEVOX speedScale, 1 = normal)\n"
          "       koe devices    list output devices (ID<TAB>name, * = default)\n"
          "       koe styles     print speaker/style metadata as JSON\n", stderr);
    return EXIT_USAGE;
}

static int run(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "devices") == 0) return list_devices();
    const char *out = NULL;
    double speed = 1.0;
    while (argc >= 3 && (strcmp(argv[1], "-o") == 0 || strcmp(argv[1], "-s") == 0)) {
        if (argv[1][1] == 'o') {
            out = argv[2];
        } else {
            char *end;
            speed = strtod(argv[2], &end);
            if (*end || !(speed > 0)) return usage();
        }
        argv += 2;
        argc -= 2;
    }
    if (argc >= 2 && argv[1][0] == '-') return usage();

    koe_config cfg;
    if (load_config(&cfg) != 0) return EXIT_FAIL;

    if (argc == 2 && strcmp(argv[1], "styles") == 0) {
        struct VoicevoxSynthesizer *synth = init_synthesizer(cfg.core);
        if (!synth) return EXIT_FAIL;
        char *json = p_voicevox_synthesizer_create_metas_json(synth);
        puts(json);
        p_voicevox_json_free(json);
        p_voicevox_synthesizer_delete(synth);
        return 0;
    }

    char *text = argc >= 2 ? join_args(argc - 1, argv + 1) : read_stdin();
    if (!text) { fputs("koe: out of memory\n", stderr); return EXIT_FAIL; }
    if (is_blank(text)) { free(text); return usage(); }

    int rc = EXIT_FAIL;
    struct VoicevoxSynthesizer *synth = init_synthesizer(cfg.core);
    if (synth) {
        uintptr_t wav_len = 0;
        uint8_t *wav = NULL;
        VoicevoxResultCode vrc = synthesize(synth, text, cfg.style, speed, &wav_len, &wav);
        if (vrc != VOICEVOX_RESULT_OK) {
            vv_fail(vrc);
        } else {
            if ((out ? write_wav(out, wav, wav_len) : play_wav(wav, wav_len, &cfg)) == 0) rc = 0;
            p_voicevox_wav_free(wav);
        }
        p_voicevox_synthesizer_delete(synth);
    }
    free(text);
    return rc;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    /* argv と A 系 API を UTF-8 にするのは実行ファイルに埋めたマニフェスト
       (koe.manifest, activeCodePage=UTF-8)。CRT の wcstombs もここで UTF-8 に合わせる。 */
    setlocale(LC_ALL, ".UTF-8");
#endif
    return run(argc, argv);
}
