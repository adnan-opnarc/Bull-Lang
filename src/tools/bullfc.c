/*
 * BullFC - Bull Project Manager
 * Creates and manages Bull project structures
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "toml.h"
#include "color.h"

#define VERSION "1.0.0"
#define DEF_ORG "user_bull"

// Scan directory for .bl source files
static int scan_src_files(const char *src_dir, char **files, int max_files) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "find \"%s\" -name '*.bl' -type f 2>/dev/null | sort", src_dir);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;
    
    int count = 0;
    char line[512];
    while (count < max_files && fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        files[count] = strdup(line);
        count++;
    }
    pclose(fp);
    return count;
}

static void print_help(const char *prog_name) {
    fprintf(stderr, "BullFC - Bull Project Manager v%s\n", VERSION);
    fprintf(stderr, "Usage: %s [options] <project_name>\n", prog_name);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --help          Show this help message\n");
    fprintf(stderr, "  --version       Show version information\n");
    fprintf(stderr, "  --update        Update bullfc.toml with current src files\n");
    fprintf(stderr, "  -C <name>       Create a new crate (library/package)\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s myproject        Create a new Bull project\n", prog_name);
    fprintf(stderr, "  %s .                Create in current directory\n", prog_name);
    fprintf(stderr, "  %s --update .       Update srcs in existing project\n", prog_name);
    fprintf(stderr, "  %s -C math          Create a new crate named 'math'\n", prog_name);
}

static void print_version(void) {
    fprintf(stderr, "BullFC version %s\n", VERSION);
    fprintf(stderr, "Bull Compiler Infrastructure\n");
}

static int dir_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int create_dir(const char *path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", path);
    return system(cmd);
}

static int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Could not create file '%s'\n", path);
        return -1;
    }
    fprintf(f, "%s", content);
    fclose(f);
    return 0;
}

static int update_project(const char *project_name) {
    (void)project_name;
    printf("Updating project configuration...\n");
    
    // Determine config file path
    const char *config_file = "bullfc.toml";
    const char *src_dir = "src";
    
    if (strcmp(project_name, ".") != 0) {
        // If project_name is a directory, use it
        char path[512];
        snprintf(path, sizeof(path), "%s/bullfc.toml", project_name);
        if (dir_exists(project_name)) {
            chdir(project_name);
        }
    }
    
    if (!dir_exists(src_dir)) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " No src directory found\n");
        return -1;
    }
    
    // Scan for .bl files
    char **src_files = malloc(100 * sizeof(char *));
    int file_count = scan_src_files("src", src_files, 100);
    
    if (file_count == 0) {
        fprintf(stderr, COLOR_ORANGE "Warning:" COLOR_RESET " No .bl files found in src/\n");
    } else {
        printf("Found %d source file(s):\n", file_count);
        for (int i = 0; i < file_count; i++) {
            printf("  %s\n", src_files[i]);
        }
    }
    
    // Read existing config
    TomlDocument *doc = toml_parse(config_file);
    if (!doc) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Could not read %s\n", config_file);
        return -1;
    }
    
    // Update or create [srcs] section
    TomlSection *srcs = NULL;
    for (int i = 0; i < doc->section_count; i++) {
        if (strcmp(doc->sections[i].name, "srcs") == 0) {
            srcs = &doc->sections[i];
            for (int j = 0; j < srcs->pair_count; j++) {
                free(srcs->pairs[j].key);
                free(srcs->pairs[j].value);
            }
            srcs->pair_count = 0;
            break;
        }
    }
    
    if (!srcs) {
        if (doc->section_count >= doc->section_capacity) {
            doc->section_capacity *= 2;
            doc->sections = realloc(doc->sections, doc->section_capacity * sizeof(TomlSection));
        }
        srcs = &doc->sections[doc->section_count++];
        srcs->name = strdup("srcs");
        srcs->pair_count = 0;
        srcs->pair_capacity = 8;
        srcs->pairs = calloc(srcs->pair_capacity, sizeof(TomlKeyValue));
    }
    
    // Add source files
    for (int i = 0; i < file_count; i++) {
        if (srcs->pair_count >= srcs->pair_capacity) {
            srcs->pair_capacity *= 2;
            srcs->pairs = realloc(srcs->pairs, srcs->pair_capacity * sizeof(TomlKeyValue));
        }
        char key[64];
        snprintf(key, sizeof(key), "src%d", i+1);
        srcs->pairs[srcs->pair_count].key = strdup(key);
        srcs->pairs[srcs->pair_count].value = strdup(src_files[i]);
        srcs->pair_count++;
    }
    
    // Write updated config
    FILE *f = fopen(config_file, "w");
    if (f) {
        for (int i = 0; i < doc->section_count; i++) {
            fprintf(f, "[%s]\n", doc->sections[i].name);
            for (int j = 0; j < doc->sections[i].pair_count; j++) {
                fprintf(f, "%s = \"%s\"\n", doc->sections[i].pairs[j].key, doc->sections[i].pairs[j].value);
            }
            fprintf(f, "\n");
        }
        fclose(f);
        printf("Updated %s\n", config_file);
    }
    
    toml_free(doc);
    for (int i = 0; i < file_count; i++) free(src_files[i]);
    free(src_files);
    return 0;
}

static int create_project(const char *project_name) {
    char src_dir[512];
    char build_dir[512];
    char crates_dir[512];
    char main_file[512];
    char config_file[512];
    
    int use_current = (strcmp(project_name, ".") == 0);
    
    if (use_current) {
        snprintf(src_dir, sizeof(src_dir), "src");
        snprintf(build_dir, sizeof(build_dir), "build");
        snprintf(crates_dir, sizeof(crates_dir), "crates");
        snprintf(main_file, sizeof(main_file), "src/main.bl");
        snprintf(config_file, sizeof(config_file), "bullfc.toml");
    } else {
        snprintf(src_dir, sizeof(src_dir), "%s/src", project_name);
        snprintf(build_dir, sizeof(build_dir), "%s/build", project_name);
        snprintf(crates_dir, sizeof(crates_dir), "%s/crates", project_name);
        snprintf(main_file, sizeof(main_file), "%s/src/main.bl", project_name);
        snprintf(config_file, sizeof(config_file), "%s/bullfc.toml", project_name);
    }
    
    if (!use_current) {
        printf("Creating project '%s'...\n", project_name);
        if (dir_exists(project_name)) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Directory '%s' already exists\n", project_name);
            return -1;
        }
        if (create_dir(project_name) != 0) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Could not create project directory\n");
            return -1;
        }
    } else {
        printf("Initializing Bull project in current directory...\n");
    }
    
    // Create directory structure
    printf("  Creating directory structure...\n");
    if (create_dir(src_dir) != 0) return -1;
    if (create_dir(build_dir) != 0) return -1;
    if (create_dir(crates_dir) != 0) return -1;
    
     // Write bullfc.toml
     printf("  Creating bullfc.toml...\n");
     char toml_content[512];
     snprintf(toml_content, sizeof(toml_content),
         "[project]\n"
         "name = \"%s\"\n"
         "org = \"com.%s.%s\"\n"
         "ver = \"1.0\"\n"
         "\n"
         "[toolchain]\n"
         "arch = \"x86_64\"\n"
         "source = \"open\"\n"
         "\n"
         "[srcs]\n"
         "main = \"src/main.bl\"\n"
         "\n"
         "[crates]\n"
        // "standard = \"0.1\"\n"
        "# Add your crates here\n" 
        "\n"
        "[build]\n"
        "target = \"x86_64_exec\"\n"
        "name.out = \"%s\"\n",
        project_name,DEF_ORG,project_name, project_name); //id for the project

    if (write_file(config_file, toml_content) != 0) return -1;
    
    // Write bpkg.toml
    printf("  Creating bpkg.toml...\n");
    const char *bpkg_content = 
        "[mirrors]\n"
        "official = \"https://github.com/adnan-opnarc\"\n";
    
    char bpkg_file[512];
    if (use_current) {
        snprintf(bpkg_file, sizeof(bpkg_file), "bpkg.toml");
    } else {
        snprintf(bpkg_file, sizeof(bpkg_file), "%s/bpkg.toml", project_name);
    }
    if (write_file(bpkg_file, bpkg_content) != 0) return -1;
    
    // Write main.bl
    printf("  Creating src/main.bl...\n");
    const char *main_content = 
        "using standard\n"
        "\n"
        "int main() {\n"
        "    print(\"Hello, Bull!\\n\");\n"
        "    return 0;\n"
        "}\n";
    
    if (write_file(main_file, main_content) != 0) return -1;
    
    // Write README
    if (!use_current) {
        char readme_file[512];
        snprintf(readme_file, sizeof(readme_file), "%s/README.md", project_name);
        printf("  Creating README.md...\n");
        const char *readme_content = "# Demo\n\nA Bull programming language project.\n\n";
        write_file(readme_file, readme_content);
    }
    
    printf("\nProject created successfully!\n");
    printf("\nTo build your project:\n");
    printf("  1. Edit src/main.bl\n");
    printf("  2. Run: bullc -- bullfc.toml\n");
    printf("\nTo run your project:\n");
    printf("  bullc -- bullfc.toml -r\n");
    
    return 0;
}

static int create_crate(const char *crate_name) {
    char src_dir[512];
    char build_dir[512];
    char config_file[512];
    char main_file[512];

    snprintf(src_dir, sizeof(src_dir), "%s/src", crate_name);
    snprintf(build_dir, sizeof(build_dir), "%s/build", crate_name);
    snprintf(config_file, sizeof(config_file), "%s/bullfc.toml", crate_name);
    snprintf(main_file, sizeof(main_file), "%s/src/main.bl", crate_name);

    printf("Creating crate '%s'...\n", crate_name);
    if (dir_exists(crate_name)) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Directory '%s' already exists\n", crate_name);
        return -1;
    }
    if (create_dir(crate_name) != 0) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Could not create crate directory\n");
        return -1;
    }

    printf("  Creating directory structure...\n");
    if (create_dir(src_dir) != 0) return -1;
    if (create_dir(build_dir) != 0) return -1;

    printf("  Creating bullfc.toml...\n");
    char toml_content[1024];
    snprintf(toml_content, sizeof(toml_content),
        "[project]\n"
        "name = \"%s\"\n"
        "org = \"com.openarc.%s\"\n"
        "ver = \"1.0\"\n"
        "\n"
        "[srcs]\n"
        "main = \"src/main.bl\"\n"
        "\n"
        "[build]\n"
        "target = \"x86_64_lib\"\n"
        "name.out = \"%s\"\n",
        crate_name, crate_name, crate_name);

    if (write_file(config_file, toml_content) != 0) return -1;

    printf("  Creating src/main.bl...\n");
    char main_content[1024];
    snprintf(main_content, sizeof(main_content),
        "using standard\n"
        "\n"
        "// Crate: %s\n"
        "// Export functions for other Bull programs to use with 'using %s'\n"
        "//\n"
        "// Usage from another project:\n"
        "//   using %s\n"
        "//   int main() {\n"
        "//       %s_hello();\n"
        "//       return 0;\n"
        "//   }\n"
        "\n"
        "glass %s_hello() {\n"
        "    print(\"Hello from %s!\\n\");\n"
        "}\n",
        crate_name, crate_name, crate_name, crate_name, crate_name, crate_name);

    if (write_file(main_file, main_content) != 0) return -1;

    printf("\nCrate '%s' created successfully!\n", crate_name);
    printf("\nTo build:\n");
    printf("  cd %s && ../bullc -ar -- bullfc.toml\n", crate_name);
    printf("\nTo use in a project:\n");
    printf("  Add to your bullfc.toml:\n");
    printf("    [crates]\n");
    printf("    %s = { path = \"path/to/%s\" }\n", crate_name, crate_name);
    printf("  Then in your .bl file:\n");
    printf("    using %s\n", crate_name);

    return 0;
}

int main(int argc, char **argv) {
    const char *project_name = NULL;
    const char *crate_name = NULL;
    int update_mode = 0;
    int crate_mode = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "--update") == 0) {
            update_mode = 1;
            continue;
        }
        if (strcmp(argv[i], "-C") == 0) {
            crate_mode = 1;
            continue;
        }
        if (argv[i][0] != '-') {
            if (crate_mode) {
                crate_name = argv[i];
            } else {
                project_name = argv[i];
            }
        }
    }
    
    if (crate_mode) {
        if (!crate_name) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " No crate name specified\n");
            fprintf(stderr, "Usage: %s -C <crate_name>\n", argv[0]);
            return 1;
        }
        return create_crate(crate_name);
    }
    
    if (!project_name) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " No project name specified\n");
        print_help(argv[0]);
        return 1;
    }
    
    if (update_mode) {
        return update_project(project_name);
    }
    
    return create_project(project_name);
}
