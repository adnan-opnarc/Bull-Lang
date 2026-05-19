#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "toml.h"
#include "color.h"

#define VERSION "1.0.0"
#define OFFICIAL_MIRROR "https://github.com/OpenArc-1"

static void print_help(const char *prog) {
    fprintf(stderr, "bpkg - Bull Package Manager v%s\n", VERSION);
    fprintf(stderr, "Usage: %s <command> [options]\n\n", prog);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  install [crate]   Install crates from bullfc.toml [crates]\n");
    fprintf(stderr, "  list              List installed crates\n");
    fprintf(stderr, "  update            Update package.lst registry\n");
    fprintf(stderr, "  --help            Show this help\n");
    fprintf(stderr, "  --version         Show version\n");
    fprintf(stderr, "\nMirror config (bpkg.toml or bullfc.toml [mirrors]):\n");
    fprintf(stderr, "  [mirrors]\n");
    fprintf(stderr, "  default = \"https://github.com/OpenArc-1\"\n");
}

static void print_version(void) {
    fprintf(stderr, "bpkg version %s\n", VERSION);
}

static int dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

static int run_cmd(const char *cmd) {
    printf("  $ %s\n", cmd);
    return system(cmd);
}

static char *read_mirror_url(void) {
    const char *configs[] = {"bpkg.toml", "bullfc.toml", NULL};

    for (int c = 0; configs[c]; c++) {
        if (!file_exists(configs[c])) continue;
        TomlDocument *doc = toml_parse(configs[c]);
        if (!doc) continue;

        const char *url = NULL;
        for (int i = 0; i < doc->section_count; i++) {
            if (strcmp(doc->sections[i].name, "mirrors") == 0
                && doc->sections[i].pair_count > 0) {
                url = doc->sections[i].pairs[0].value;
                break;
            }
        }

        if (url) {
            char *mirror_url = strdup(url);
            toml_free(doc);
            size_t len = strlen(mirror_url);
            while (len > 0 && mirror_url[len-1] == '/') mirror_url[--len] = '\0';
            printf("  mirror: %s (from %s)\n", mirror_url, configs[c]);
            return mirror_url;
        }
        toml_free(doc);
    }

    return NULL;
}

static char *crate_git_url(const char *name, const char *mirror) {
    char *url = malloc(1024);
    if (mirror)
        snprintf(url, 1024, "%s/%s.git", mirror, name);
    else
        snprintf(url, 1024, "%s/%s.git", OFFICIAL_MIRROR, name);
    return url;
}

static int install_crate(const char *name, const char *mirror) {
    char crate_dir[1024], build_dir[1024], a_file[1024];
    snprintf(crate_dir, sizeof(crate_dir), "crates/%s", name);
    snprintf(build_dir, sizeof(build_dir), "%s/build", crate_dir);
    snprintf(a_file, sizeof(a_file), "%s/%s.a", build_dir, name);

    if (file_exists(a_file)) {
        printf("  [skip] %s (already built)\n", name);
        return 0;
    }

    if (!dir_exists(crate_dir)) {
        char *url = crate_git_url(name, mirror);
        printf("  cloning %s\n", name);
        printf("    from: %s\n", url);
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "git clone \"%s\" \"%s\"", url, crate_dir);
        int rc = run_cmd(cmd);
        free(url);
        if (rc != 0) {
            fprintf(stderr, COLOR_RED "  error:" COLOR_RESET " failed to clone %s\n", name);
            return -1;
        }
    } else {
        printf("  [found] %s already cloned\n", name);
    }

    printf("  building %s\n", name);

    if (!dir_exists(build_dir)) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", build_dir);
        run_cmd(cmd);
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && bullc -- bullfc.toml -ar -o \"build/%s.a\" 2>&1",
             crate_dir, name);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, COLOR_RED "  error:" COLOR_RESET " build failed for %s\n", name);
        return -1;
    }

    printf("  done %s\n", name);
    return 0;
}

static int install_all(const char *mirror) {
    if (!file_exists("bullfc.toml")) {
        fprintf(stderr, COLOR_RED "error:" COLOR_RESET " no bullfc.toml found in current directory\n");
        return -1;
    }

    TomlDocument *doc = toml_parse("bullfc.toml");
    if (!doc) {
        fprintf(stderr, COLOR_RED "error:" COLOR_RESET " cannot parse bullfc.toml\n");
        return -1;
    }

    int found = 0;
    for (int i = 0; i < doc->section_count; i++) {
        if (strcmp(doc->sections[i].name, "crates") == 0) {
            found = doc->sections[i].pair_count;
            printf("found %d crate(s) in bullfc.toml\n", found);
            for (int j = 0; j < doc->sections[i].pair_count; j++) {
                printf("\n[%d/%d] %s\n", j + 1, doc->sections[i].pair_count,
                       doc->sections[i].pairs[j].key);
                install_crate(doc->sections[i].pairs[j].key, mirror);
            }
            break;
        }
    }

    toml_free(doc);

    if (found == 0)
        printf("no crates found in [crates] section\n");
    return 0;
}

static int list_crates(void) {
    if (!dir_exists("crates")) {
        printf("no crates installed (crates/ directory missing)\n");
        return 0;
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "ls -1d crates/*/ 2>/dev/null | while read d; do basename \"$d\"; done");
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        printf("no crates installed\n");
        return 0;
    }

    int count = 0;
    char line[256];
    printf("installed crates:\n");
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        char a_path[1024];
        snprintf(a_path, sizeof(a_path), "crates/%s/build/%s.a", line, line);
        printf("  %s", line);
        if (file_exists(a_path))
            printf(" (built)");
        else
            printf(" (unbuilt)");
        printf("\n");
        count++;
    }
    pclose(fp);

    if (count == 0) printf("  (none)\n");
    return 0;
}

static int update_registry(const char *mirror) {
    const char *pkg_list = "package.lst";
    FILE *f = fopen(pkg_list, "w");
    if (!f) {
        fprintf(stderr, COLOR_RED "error:" COLOR_RESET " cannot create %s\n", pkg_list);
        return -1;
    }

    const char *base = mirror ? mirror : OFFICIAL_MIRROR;
    fprintf(f, "; bpkg registry\n");
    fprintf(f, "; auto-generated by bpkg update\n\n");
    fprintf(f, "[mirrors]\n");
    fprintf(f, "default = \"%s\"\n", base);
    fclose(f);

    printf("created %s (mirror: %s)\n", pkg_list, base);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0) {
        print_version();
        return 0;
    }

    if (strcmp(argv[1], "install") == 0) {
        char *mirror = read_mirror_url();
        if (!mirror)
            printf("  mirror: %s (default)\n", OFFICIAL_MIRROR);
        if (argc >= 3) {
            for (int i = 2; i < argc; i++) {
                printf("\n[1/1] %s\n", argv[i]);
                install_crate(argv[i], mirror);
            }
        } else {
            install_all(mirror);
        }
        free(mirror);
        return 0;
    }

    if (strcmp(argv[1], "list") == 0) {
        return list_crates();
    }

    if (strcmp(argv[1], "update") == 0) {
        char *mirror = read_mirror_url();
        int r = update_registry(mirror);
        free(mirror);
        return r;
    }

    fprintf(stderr, COLOR_RED "error:" COLOR_RESET " unknown command '%s'\n", argv[1]);
    print_help(argv[0]);
    return 1;
}
