/* A stand-in for the player kit's Arkeyan-Builder (docs/install.md): prints its events with delays, so the launcher's install
 * screens can be tried without the real kit. Built as fake-kit/Arkeyan-Builder in the build folder (target FakeArkeyanBuilder);
 * point the launcher at that folder with SKY_LAUNCHER_KIT.
 * Same steps, titles and order as the real one: toolchain, disc, recompile, shaders, compile, link, movies, then the
 * second shaders/compile/link pass that follows the movie shaders.
 *   FAKE_SSABUILD_FAIL=<step id>  fail half way through that step (toolchain, disc, recompile, shaders, compile, link,
 *                                 movies)
 *   FAKE_SSABUILD_DELAY=<seconds> pause between events (default 0.05)
 *   FAKE_SSABUILD_HOLD=<seconds>  wait after the last step, before finishing (to look at a build at 100 %)
 * --hd-textures off|auto: as the real one, a last step "hdtextures" after the build, or alone (no --out) as the only step;
 *   FAKE_SSABUILD_FAIL=hdtextures fails half way through it, and FAKE_SSABUILD_HD_MARKER=<file> gets the mode it was asked for. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#define MAKE_DIR(path) _mkdir(path)
#define GAME "SSARecomp.exe"
#else
#include <sys/stat.h>
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#define MAKE_DIR(path) mkdir(path, 0755)
#define GAME "SSARecomp"
#endif

static int g_delay = 50;
static int g_step = 0;

static void step(const char* id, const char* title, int count)
{
    const char* fail = getenv("FAKE_SSABUILD_FAIL");
    const char* failStep = getenv("FAKE_SSABUILD_FAIL_STEP");
    const char* pauseStep = getenv("FAKE_SSABUILD_PAUSE_STEP");
    ++g_step;
    printf("@step %s %s\n", id, title);
    for (int i = 1; i <= count; i++)
    {
        printf("@progress %d %d\n", i, count);
        if (i % 10 == 0)
            printf("@log %s: file %d of %d\n", id, i, count);
        if (((fail != NULL && strcmp(fail, id) == 0) || (failStep != NULL && atoi(failStep) == g_step)) && i == count / 2)
        {
            fflush(stdout);
            fprintf(stderr, "clang: error: something went wrong in item %d\n", i);
            fflush(stderr);
            printf("@error %s failed: item %d didn't compile.\n", title, i);
            exit(1);
        }
        fflush(stdout);
        /* Hold after observable progress, so cancellation tests synchronize on an event, not a delay. */
        if (i == 1 && pauseStep != NULL && atoi(pauseStep) == g_step)
            SLEEP_MS(30000);
        SLEEP_MS(g_delay);
    }
}

/* --menu-music <file.wav>: a 0.1 s stereo 16-bit WAV of silence at 8 kHz, as the real one writes the game's theme.
 * FAKE_SSABUILD_NO_MUSIC set: fails, as a kit before 0.6.1 does (it does not know the option). */
static int menu_music(const char* path)
{
    if (getenv("FAKE_SSABUILD_NO_MUSIC") != NULL)
    {
        printf("@error unknown option --menu-music\n");
        return 2;
    }
    FILE* file = fopen(path, "wb");
    if (file == NULL)
        return 1;
    const unsigned frames = 800, bytes = frames * 4;
    unsigned char head[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
        0x40, 0x1F, 0, 0, 0, 0x7D, 0, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0 };
    unsigned total = 36 + bytes;
    memcpy(head + 4, &total, 4);
    memcpy(head + 40, &bytes, 4);
    fwrite(head, 1, sizeof head, file);
    for (unsigned i = 0; i < bytes; i++)
        fputc(0, file);
    fclose(file);
    printf("@done %s\n", path);
    return 0;
}

/* The HD texture step: "hdtextures" is a step like the others; the mode is written to the marker file when the test names one. */
static void hd_textures(const char* mode)
{
    const char* marker = getenv("FAKE_SSABUILD_HD_MARKER");
    if (marker != NULL)
    {
        FILE* file = fopen(marker, "wb");
        if (file != NULL)
        {
            fprintf(file, "%s\n", mode);
            fclose(file);
        }
    }
    step("hdtextures", "Making HD textures", strcmp(mode, "off") == 0 ? 2 : 40);
}

int main(int argc, char** argv)
{
    if (getenv("FAKE_SSABUILD_CLOSE_OUTPUT") != NULL)
    {
#if !defined(_WIN32)
        signal(SIGTERM, SIG_IGN);
#endif
        fclose(stdout);
        fclose(stderr);
        for (int i = 0; i < 500; i++) SLEEP_MS(10);
        return 1;
    }
    const char *iso = NULL, *out = NULL, *hd = NULL;
    for (int i = 1; i + 1 < argc; i++)
    {
        if (strcmp(argv[i], "--iso") == 0)
            iso = argv[++i];
        else if (strcmp(argv[i], "--out") == 0)
            out = argv[++i];
        else if (strcmp(argv[i], "--hd-textures") == 0)
            hd = argv[++i];
        else if (strcmp(argv[i], "--menu-music") == 0)
            return menu_music(argv[i + 1]);
    }
    if (getenv("FAKE_SSABUILD_DELAY") != NULL)
        g_delay = (int)(atof(getenv("FAKE_SSABUILD_DELAY")) * 1000);
    if (iso != NULL && out == NULL && hd != NULL) /* the texture step alone */
    {
        printf("@log fake Arkeyan-Builder, iso %s\n", iso);
        hd_textures(hd);
        printf("@done textures-hd\n");
        return 0;
    }
    if (iso == NULL || out == NULL)
    {
        printf("@error usage: Arkeyan-Builder --iso <iso> --out <game dir>\n");
        return 2;
    }

    printf("@log fake Arkeyan-Builder, iso %s\n", iso);
    const char* ending = getenv("FAKE_SSABUILD_ENDING");
    if (ending != NULL)
    {
        if (strcmp(ending, "error") == 0)
            fputs("@error synthetic failure without newline", stdout);
        else if (strcmp(ending, "exit") == 0)
            return 7;
        return 0;
    }
    step("toolchain", "Preparing the compiler", 10);
    step("disc", "Reading the disc", 10);
    step("recompile", "Translating the game code", 40);
    step("shaders", "Translating the shaders", 30);
    step("compile", "Compiling the game", 80);
    step("link", "Linking the game", 5);
    step("movies", "Adding the movie shaders", 10);
    step("shaders", "Translating the shaders", 5);
    step("compile", "Compiling the game", 5);
    step("link", "Linking the game", 2);
    if (hd != NULL)
        hd_textures(hd);
    fflush(stdout);
    if (getenv("FAKE_SSABUILD_HOLD") != NULL)
        SLEEP_MS((int)(atof(getenv("FAKE_SSABUILD_HOLD")) * 1000));
    printf("@log finished\n");

    char path[4096];
    MAKE_DIR(out);
    snprintf(path, sizeof(path), "%s/%s", out, GAME);
    FILE* game = fopen(path, "wb");
    if (game == NULL)
    {
        printf("@error can't write %s\n", path);
        return 1;
    }
    fputs("fake game\n", game);
    fclose(game);
    printf("@done %s\n", path);
    return 0;
}
