#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "toml.h"
#include "color.h"

#define VERSION "1.0.0"
#define BULLV_DIR ".bullv"
#define INF_FILE "inf.txt"
#define SNAP_DIR ".bullv/snapshots"
#define HEAD_FILE ".bullv/HEAD"
#define SNAP_META "snapshot.txt"
#define ID_LEN 8

static void print_help(const char *prog) {
    fprintf(stderr, "bullv - Bull Version & Build Artifact Manager v%s\n", VERSION);
    fprintf(stderr, "Usage: %s <command> [options]\n\n", prog);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  init                Initialize version tracking in project\n");
    fprintf(stderr, "  generate            Generate inf.txt from project config\n");
    fprintf(stderr, "  info                Show current version and build info\n");
    fprintf(stderr, "  list                List build artifacts in .bullv/\n");
    fprintf(stderr, "  log                 Show snapshot history\n");
    fprintf(stderr, "  -a \"<message>\"       Snapshot current source state\n");
    fprintf(stderr, "  -s <id> [path]      Restore a snapshot to a directory\n");
    fprintf(stderr, "  -d <id>             Delete a snapshot\n");
    fprintf(stderr, "  -m <id> \"<message>\"  Amend a snapshot's message\n");
    fprintf(stderr, "  --help              Show this help\n");
    fprintf(stderr, "  --version           Show version\n");
}

static void print_version(void) {
    fprintf(stderr, "bullv version %s\n", VERSION);
}

static int dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

static int create_dir(const char *path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", path);
    return system(cmd);
}

static char *read_config_value(const char *section, const char *key) {
    if (!file_exists("bullfc.toml")) return NULL;
    TomlDocument *doc = toml_parse("bullfc.toml");
    if (!doc) return NULL;
    const char *val = toml_get_string(doc, section, key);
    char *result = val ? strdup(val) : NULL;
    toml_free(doc);
    return result;
}

static char *read_file_contents(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

static int copy_overwrite_file(const char *src, const char *dst) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cp -f \"%s\" \"%s\"", src, dst);
    return system(cmd);
}

static int copy_overwrite_dir(const char *src, const char *dst) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cp -rf \"%s\"/* \"%s/\"", src, dst);
    return system(cmd);
}

static int copy_dir_contents(const char *src, const char *dst) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cp -rf \"%s\"/* \"%s/\"", src, dst);
    return system(cmd);
}

static int remove_dir(const char *path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
    return system(cmd);
}

static void generate_random_id(char *buf, int len) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < len; i++)
        buf[i] = chars[rand() % (sizeof(chars) - 1)];
    buf[len] = '\0';
}

static char *current_timestamp(void) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char *buf = malloc(64);
    strftime(buf, 64, "%Y-%m-%d %H:%M:%S", tm_info);
    return buf;
}

static int is_valid_id(const char *id) {
    int len = strlen(id);
    if (len != ID_LEN) return 0;
    for (int i = 0; i < len; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            return 0;
    }
    return 1;
}

static char *read_head(void) {
    if (!file_exists(HEAD_FILE)) return NULL;
    return read_file_contents(HEAD_FILE);
}

static int write_head(const char *id) {
    FILE *f = fopen(HEAD_FILE, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", id);
    fclose(f);
    return 0;
}

static char *snapshot_path(const char *id) {
    char *path = malloc(512);
    snprintf(path, 512, "%s/%s", SNAP_DIR, id);
    return path;
}

static char *snapshot_meta_path(const char *id) {
    char *path = malloc(512);
    snprintf(path, 512, "%s/%s/%s", SNAP_DIR, id, SNAP_META);
    return path;
}

static int cmd_init(void) {
    printf("Initializing version tracking...\n");
    if (!dir_exists(BULLV_DIR)) {
        if (create_dir(BULLV_DIR) != 0) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Could not create %s/\n", BULLV_DIR);
            return -1;
        }
        printf("  created %s/\n", BULLV_DIR);
    } else {
        printf("  %s/ already exists\n", BULLV_DIR);
    }
    if (!dir_exists(SNAP_DIR)) {
        if (create_dir(SNAP_DIR) != 0) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Could not create %s/\n", SNAP_DIR);
            return -1;
        }
        printf("  created %s/\n", SNAP_DIR);
    }
    char inf_path[512];
    snprintf(inf_path, sizeof(inf_path), "%s/%s", BULLV_DIR, INF_FILE);
    if (!file_exists(inf_path)) {
        char *pkgname = read_config_value("project", "name");
        char *org = read_config_value("project", "org");
        char *ver = read_config_value("project", "ver");
        char *ts = current_timestamp();
        FILE *f = fopen(inf_path, "w");
        if (f) {
            fprintf(f, "; bullv artifact info\n");
            fprintf(f, "; generated by bullv init\n\n");
            fprintf(f, "pkgname = \"%s\"\n", pkgname ? pkgname : "unknown");
            fprintf(f, "org = \"%s\"\n", org ? org : "unknown");
            fprintf(f, "version = \"%s\"\n", ver ? ver : "1.0");
            fprintf(f, "built = \"%s\"\n", ts);
            fprintf(f, "arch = \"x86_64\"\n");
            fclose(f);
            printf("  created %s\n", inf_path);
        }
        free(pkgname); free(org); free(ver); free(ts);
    } else {
        printf("  %s already exists\n", inf_path);
    }
    printf("Done.\n");
    return 0;
}

static int cmd_generate(void) {
    if (!file_exists("bullfc.toml")) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " no bullfc.toml found\n");
        return -1;
    }
    if (!dir_exists(BULLV_DIR)) {
        fprintf(stderr, COLOR_ORANGE "Warning:" COLOR_RESET " %s/ not found, run 'bullv init' first\n", BULLV_DIR);
        create_dir(BULLV_DIR);
    }
    char *pkgname = read_config_value("project", "name");
    char *org = read_config_value("project", "org");
    char *ver = read_config_value("project", "ver");
    char *target = read_config_value("build", "target");
    char *name_out = read_config_value("build", "name.out");
    char *ts = current_timestamp();
    char inf_path[512];
    snprintf(inf_path, sizeof(inf_path), "%s/%s", BULLV_DIR, INF_FILE);
    FILE *f = fopen(inf_path, "w");
    if (!f) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Could not write %s\n", inf_path);
        free(pkgname); free(org); free(ver); free(target); free(name_out); free(ts);
        return -1;
    }
    fprintf(f, "; bullv artifact info\n");
    fprintf(f, "; auto-generated by bullv generate\n\n");
    fprintf(f, "pkgname = \"%s\"\n", pkgname ? pkgname : "unknown");
    fprintf(f, "org = \"%s\"\n", org ? org : "unknown");
    fprintf(f, "version = \"%s\"\n", ver ? ver : "1.0");
    fprintf(f, "built = \"%s\"\n", ts);
    fprintf(f, "target = \"%s\"\n", target ? target : "x86_64");
    fprintf(f, "name.out = \"%s\"\n", name_out ? name_out : "out");
    fclose(f);
    printf("  generated %s\n", inf_path);
    printf("  org: %s\n", org ? org : "unknown");
    free(pkgname); free(org); free(ver); free(target); free(name_out); free(ts);
    return 0;
}

static int cmd_info(void) {
    printf("Bullv project info:\n");
    char *head = read_head();
    if (head) {
        head[strcspn(head, "\n")] = '\0';
        printf("  HEAD: %s", head);
        char *mpath = snapshot_meta_path(head);
        if (file_exists(mpath)) {
            char *meta = read_file_contents(mpath);
            if (meta) {
                char *msg = strstr(meta, "message = ");
                if (msg) {
                    msg += 10;
                    char *end = strchr(msg, '\n');
                    if (end) *end = '\0';
                    printf("  %s", msg);
                }
                free(meta);
            }
        }
        free(mpath);
        printf("\n");
        free(head);
    } else {
        printf("  HEAD: (none)\n");
    }
    char inf_path[512];
    snprintf(inf_path, sizeof(inf_path), "%s/%s", BULLV_DIR, INF_FILE);
    if (file_exists(inf_path)) {
        printf("\nBuild version info:\n");
        FILE *f = fopen(inf_path, "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                if (line[0] != ';' && line[0] != '\n')
                    printf("  %s", line);
            }
            fclose(f);
        }
    }
    return 0;
}

static int cmd_list(void) {
    if (!dir_exists(BULLV_DIR)) {
        printf("No artifacts directory (%s/) found.\n", BULLV_DIR);
        return 0;
    }
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "find %s/ -type f ! -path '%s/*' 2>/dev/null | sort", BULLV_DIR, SNAP_DIR);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        printf("No artifacts found.\n");
        return 0;
    }
    int count = 0;
    char line[512];
    printf("Build artifacts:\n");
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        printf("  %s", line);
        struct stat st;
        if (stat(line, &st) == 0)
            printf(" (%ld bytes)", (long)st.st_size);
        printf("\n");
        count++;
    }
    pclose(fp);
    if (count == 0) printf("  (none)\n");
    else printf("\nTotal: %d artifact(s)\n", count);
    return 0;
}

static int cmd_snapshot_add(const char *message) {
    if (!dir_exists(SNAP_DIR)) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " not initialized (run 'bullv init' first)\n");
        return -1;
    }
    if (!dir_exists("src")) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " no src/ directory found\n");
        return -1;
    }
    srand(time(NULL) ^ (getpid() << 16));
    char id[ID_LEN + 1];
    generate_random_id(id, ID_LEN);

    char *spath = snapshot_path(id);
    create_dir(spath);
    char *spath_src = malloc(strlen(spath) + 10);
    sprintf(spath_src, "%s/src", spath);
    create_dir(spath_src);
    copy_dir_contents("src", spath_src);
    if (file_exists("bullfc.toml"))
        copy_overwrite_file("bullfc.toml", spath);
    if (file_exists("bpkg.toml"))
        copy_overwrite_file("bpkg.toml", spath);

    char *s_meta = malloc(strlen(spath) + 20);
    sprintf(s_meta, "%s/%s", spath, SNAP_META);
    free(spath_src);

    char *parent = read_head();
    char *ts = current_timestamp();
    FILE *f = fopen(s_meta, "w");
    if (f) {
        fprintf(f, "id = \"%s\"\n", id);
        fprintf(f, "message = \"%s\"\n", message);
        fprintf(f, "timestamp = \"%s\"\n", ts);
        fprintf(f, "parent = \"%s\"\n", parent ? parent : "(none)");
        fclose(f);
    }
    free(ts);
    free(s_meta);

    write_head(id);

    printf(COLOR_GREEN "  [%s]" COLOR_RESET " %s\n", id, message);
    free(spath);
    free(parent);
    return 0;
}

static int cmd_snapshot_restore(const char *id, const char *dest_path) {
    if (!dir_exists(SNAP_DIR)) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " no snapshots found (run 'bullv init' first)\n");
        return -1;
    }
    if (!is_valid_id(id)) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " invalid snapshot id '%s' (expected %d chars)\n", id, ID_LEN);
        return -1;
    }
    char *spath = snapshot_path(id);
    if (!dir_exists(spath)) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " snapshot '%s' does not exist\n", id);
        free(spath);
        return -1;
    }
    char *s_src = malloc(strlen(spath) + 10);
    sprintf(s_src, "%s/src", spath);
    if (!dir_exists(s_src)) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " snapshot '%s' has no src/ directory\n", id);
        free(spath); free(s_src);
        return -1;
    }

    const char *target = dest_path ? dest_path : ".";
    printf("Restoring snapshot %s to %s\n", id, target);

    if (strcmp(target, ".") == 0 || strcmp(target, "./") == 0) {
        remove_dir("src");
        create_dir("src");
        copy_overwrite_dir(s_src, "src");
        char *cfg_src = malloc(strlen(spath) + 20);
        sprintf(cfg_src, "%s/bullfc.toml", spath);
        if (file_exists(cfg_src))
            copy_overwrite_file(cfg_src, "bullfc.toml");
        free(cfg_src);
        char *bpkg_src = malloc(strlen(spath) + 20);
        sprintf(bpkg_src, "%s/bpkg.toml", spath);
        if (file_exists(bpkg_src))
            copy_overwrite_file(bpkg_src, "bpkg.toml");
        free(bpkg_src);
        write_head(id);
    } else {
        remove_dir(target);
        create_dir(target);
        copy_dir_contents(spath, target);
        char head_path[1024];
        snprintf(head_path, sizeof(head_path), "%s/%s", target, HEAD_FILE);
        char head_dir[1024];
        snprintf(head_dir, sizeof(head_dir), "%s/%s", target, BULLV_DIR);
        create_dir(head_dir);
        FILE *f = fopen(head_path, "w");
        if (f) { fprintf(f, "%s\n", id); fclose(f); }
    }

    printf(COLOR_GREEN "  restored %s\n" COLOR_RESET, id);
    free(spath); free(s_src);
    return 0;
}

static int cmd_snapshot_delete(const char *id) {
    if (!is_valid_id(id)) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " invalid snapshot id\n");
        return -1;
    }
    char *spath = snapshot_path(id);
    if (!dir_exists(spath)) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " snapshot '%s' does not exist\n", id);
        free(spath);
        return -1;
    }
    char *mpath = snapshot_meta_path(id);
    char *msg = NULL;
    if (file_exists(mpath)) {
        char *meta = read_file_contents(mpath);
        if (meta) {
            char *m = strstr(meta, "message = ");
            if (m) {
                m += 10;
                char *end = strchr(m, '\n');
                if (end) *end = '\0';
                msg = strdup(m);
            }
            free(meta);
        }
    }
    free(mpath);

    remove_dir(spath);
    printf("  deleted [%s] %s\n", id, msg ? msg : "");
    free(msg);

    char *head = read_head();
    if (head) {
        head[strcspn(head, "\n")] = '\0';
        if (strcmp(head, id) == 0) {
            remove(HEAD_FILE);
            printf("  HEAD was pointing to deleted snapshot, cleared\n");
        }
        free(head);
    }
    free(spath);
    return 0;
}

static int cmd_snapshot_amend(const char *id, const char *new_msg) {
    if (!is_valid_id(id)) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " invalid snapshot id\n");
        return -1;
    }
    char *mpath = snapshot_meta_path(id);
    if (!file_exists(mpath)) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " snapshot '%s' does not exist\n", id);
        free(mpath);
        return -1;
    }
    char *meta = read_file_contents(mpath);
    if (!meta) { free(mpath); return -1; }

    char *ts = current_timestamp();
    char *parent = "(none)";
    char *p = strstr(meta, "parent = ");
    if (p) {
        p += 9;
        char *end = strchr(p, '\n');
        if (end) {
            size_t plen = end - p;
            char *raw = malloc(plen + 1);
            memcpy(raw, p, plen);
            raw[plen] = '\0';
            char *uq = raw;
            if (*uq == '"') { uq++; plen--; }
            if (plen > 0 && uq[plen-1] == '"') uq[plen-1] = '\0';
            parent = strdup(uq);
            free(raw);
        }
    }

    FILE *f = fopen(mpath, "w");
    if (f) {
        fprintf(f, "id = \"%s\"\n", id);
        fprintf(f, "message = \"%s\"\n", new_msg);
        fprintf(f, "timestamp = \"%s\"\n", ts);
        fprintf(f, "parent = \"%s\"\n", parent);
        fclose(f);
        printf("  [%s] message updated\n", id);
    }
    free(ts);
    if (parent && strcmp(parent, "(none)") != 0) free(parent);
    free(meta); free(mpath);
    return 0;
}

static int cmd_log(void) {
    if (!dir_exists(SNAP_DIR)) {
        printf("No snapshots.\n");
        return 0;
    }
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "ls -1 \"%s\" 2>/dev/null | sort -r", SNAP_DIR);
    FILE *fp = popen(cmd, "r");
    if (!fp) { printf("No snapshots.\n"); return 0; }

    char *head = read_head();
    if (head) head[strcspn(head, "\n")] = '\0';

    int count = 0;
    char line[ID_LEN + 2];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) != ID_LEN) continue;
        if (!is_valid_id(line)) continue;

        char *mpath = snapshot_meta_path(line);
        char *msg = NULL, *ts = NULL;
        if (file_exists(mpath)) {
            char *meta = read_file_contents(mpath);
            if (meta) {
                char *m = strstr(meta, "message = ");
                if (m) {
                    m += 10;
                    char *end = strchr(m, '\n');
                    if (end) *end = '\0';
                    msg = strdup(m);
                }
                m = strstr(meta, "timestamp = ");
                if (m) {
                    m += 12;
                    char *end = strchr(m, '\n');
                    if (end) *end = '\0';
                    ts = strdup(m);
                }
                free(meta);
            }
        }
        free(mpath);

        int is_head = (head && strcmp(line, head) == 0);
        printf(COLOR_GREEN "  [%s]" COLOR_RESET "%s %s\n",
               line, is_head ? " HEAD" : "      ", msg ? msg : "");
        if (ts) printf("         %s\n", ts);
        count++;
        free(msg); free(ts);
    }
    pclose(fp);
    free(head);
    if (count == 0) printf("  (no snapshots)\n");
    printf("\n%d snapshot(s)\n", count);
    return 0;
}

static int cmd_current_id(void) {
    char *head = read_head();
    if (head) {
        head[strcspn(head, "\n")] = '\0';
        printf("%s\n", head);
        free(head);
    }
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
    if (strcmp(argv[1], "init") == 0)
        return cmd_init();
    if (strcmp(argv[1], "generate") == 0)
        return cmd_generate();
    if (strcmp(argv[1], "info") == 0)
        return cmd_info();
    if (strcmp(argv[1], "list") == 0)
        return cmd_list();
    if (strcmp(argv[1], "log") == 0)
        return cmd_log();
    if (strcmp(argv[1], "id") == 0)
        return cmd_current_id();

    if (strcmp(argv[1], "-a") == 0) {
        if (argc < 3) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " -a requires a message\n");
            return 1;
        }
        return cmd_snapshot_add(argv[2]);
    }
    if (strcmp(argv[1], "-s") == 0) {
        if (argc < 3) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " -s requires a snapshot id\n");
            return 1;
        }
        const char *dest = (argc >= 4 && argv[3][0] != '-') ? argv[3] : ".";
        return cmd_snapshot_restore(argv[2], dest);
    }
    if (strcmp(argv[1], "-d") == 0) {
        if (argc < 3) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " -d requires a snapshot id\n");
            return 1;
        }
        return cmd_snapshot_delete(argv[2]);
    }
    if (strcmp(argv[1], "-m") == 0) {
        if (argc < 4) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " -m requires <id> <message>\n");
            return 1;
        }
        return cmd_snapshot_amend(argv[2], argv[3]);
    }

    fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " unknown command '%s'\n", argv[1]);
    print_help(argv[0]);
    return 1;
}
