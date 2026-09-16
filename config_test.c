#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

static void test_default(void) {
    koe_config c;
    koe_config_default(&c, "/home/x/.local/share");
    assert(strcmp(c.core, "/home/x/.local/share/koe/voicevox_core") == 0);
    assert(c.style == 3);
    assert(c.device[0] == '\0');
    assert(c.volume == 1.0);
}

static void test_parse_line(void) {
    koe_config c;
    koe_config_default(&c, "/h");
    assert(koe_config_parse_line(&c, "core=/opt/vv\n") == 0);
    assert(strcmp(c.core, "/opt/vv") == 0);
    assert(koe_config_parse_line(&c, "style=2") == 0);
    assert(c.style == 2);
    assert(koe_config_parse_line(&c, "device=alsa_output.pci-0000_04_00.6.analog-stereo") == 0);
    assert(strcmp(c.device, "alsa_output.pci-0000_04_00.6.analog-stereo") == 0);
    assert(koe_config_parse_line(&c, "volume=0.5") == 0);
    assert(fabs(c.volume - 0.5) < 1e-9);
}

static void test_ignored_lines(void) {
    koe_config c;
    koe_config_default(&c, "/h");
    assert(koe_config_parse_line(&c, "") == 0);
    assert(koe_config_parse_line(&c, "\n") == 0);
    assert(koe_config_parse_line(&c, "# comment\n") == 0);
    assert(koe_config_parse_line(&c, "unknown=1\n") == 0);
    assert(c.style == 3);
}

static void test_whitespace(void) {
    koe_config c;
    koe_config_default(&c, "/h");
    assert(koe_config_parse_line(&c, "  style = 7 \r\n") == 0);
    assert(c.style == 7);
    assert(koe_config_parse_line(&c, "device=\n") == 0);
    assert(c.device[0] == '\0');
}

static void test_invalid(void) {
    koe_config c;
    koe_config_default(&c, "/h");
    assert(koe_config_parse_line(&c, "style=abc") == -1);
    assert(koe_config_parse_line(&c, "volume=loud") == -1);
    assert(koe_config_parse_line(&c, "noequals") == -1);
    assert(c.style == 3);
    assert(c.volume == 1.0);
}

static void test_parse_file(void) {
    koe_config c;
    koe_config_default(&c, "/h");
    FILE *fp = tmpfile();
    assert(fp);
    fputs("# koe\n\ncore=/vv\nstyle=1\n", fp);
    rewind(fp);
    assert(koe_config_parse(&c, fp) == 0);
    fclose(fp);
    assert(strcmp(c.core, "/vv") == 0);
    assert(c.style == 1);

    fp = tmpfile();
    fputs("style=1\nvolume=x\n", fp);
    rewind(fp);
    assert(koe_config_parse(&c, fp) == -1);
    fclose(fp);
}

int main(void) {
    test_default();
    test_parse_line();
    test_ignored_lines();
    test_whitespace();
    test_invalid();
    test_parse_file();
    puts("config_test: ok");
    return 0;
}
