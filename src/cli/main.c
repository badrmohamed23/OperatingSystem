/* src/cli/main.c – Command-line interface and entry point */
#include "os_shared.h"

static SchedulerType scheduler_from_name(const char *name)
{
    if (!name) return SCHED_HRRN;
    if (strcmp(name, "RR") == 0) return SCHED_RR;
    if (strcmp(name, "MLFQ") == 0) return SCHED_MLFQ;
    return SCHED_HRRN;
}

static const char *find_matching_brace(const char *start)
{
    if (!start || *start != '{') return NULL;
    int depth = 0;
    for (const char *p = start; *p; ++p) {
        if (*p == '{') depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0) return p;
        }
    }
    return NULL;
}

/* Simple JSON parser for stdin --json mode */
static void parse_json_config(SimulationConfig *config, const char *json_str)
{
    if (!config || !json_str) return;

    /* Scheduler */
    const char *p = strstr(json_str, "\"scheduler\"");
    if (!p) p = strstr(json_str, "\"algorithm\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            char sched_buf[32] = {0};
            if (sscanf(p + 1, " \"%31[^\"]\"", sched_buf) == 1) {
                config->algorithm = scheduler_from_name(sched_buf);
            }
        }
    }

    /* RR Quantum */
    p = strstr(json_str, "\"rrQuantum\"");
    if (!p) p = strstr(json_str, "\"quantum\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            int q = 2;
            if (sscanf(p + 1, " %d", &q) == 1) {
                config->rr_quantum = q > 0 ? q : 2;
            }
        }
    }

    /* Programs directory */
    p = strstr(json_str, "\"programs\"");
    if (!p) p = strstr(json_str, "\"programsDir\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            sscanf(p + 1, " \"%255[^\"]\"", config->programs_dir);
        }
    }

    /* Sandbox directory */
    p = strstr(json_str, "\"sandbox\"");
    if (!p) p = strstr(json_str, "\"sandboxDir\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            sscanf(p + 1, " \"%255[^\"]\"", config->sandbox_dir);
        }
    }

    /* Processes array */
    p = strstr(json_str, "\"processes\"");
    if (p) {
        p = strchr(p, '[');
        if (p) {
            config->process_count = 0;
            while (*p && *p != ']' && config->process_count < MAX_PROCESSES) {
                const char *obj = strchr(p, '{');
                if (!obj) break;
                const char *obj_end = find_matching_brace(obj);
                if (!obj_end) break;

                int idx = config->process_count++;
                ProcessSpec *ps = &config->processes[idx];
                ps->pid = idx + 1;
                ps->arrival_time = 0;
                ps->input_map.count = 0;
                ps->file[0] = '\0';

                /* Parse pid */
                const char *pid_p = strstr(obj, "\"pid\"");
                if (pid_p && pid_p < obj_end) {
                    pid_p = strchr(pid_p, ':');
                    if (pid_p) sscanf(pid_p + 1, " %d", &ps->pid);
                }

                /* Parse file */
                const char *file_p = strstr(obj, "\"file\"");
                if (!file_p) file_p = strstr(obj, "\"program\"");
                if (file_p && file_p < obj_end) {
                    file_p = strchr(file_p, ':');
                    if (file_p) sscanf(file_p + 1, " \"%127[^\"]\"", ps->file);
                }

                /* Parse arrival */
                const char *arr_p = strstr(obj, "\"arrival\"");
                if (!arr_p) arr_p = strstr(obj, "\"arrivalTime\"");
                if (arr_p && arr_p < obj_end) {
                    arr_p = strchr(arr_p, ':');
                    if (arr_p) sscanf(arr_p + 1, " %d", &ps->arrival_time);
                }

                /* Parse inputMap */
                const char *map_p = strstr(obj, "\"inputMap\"");
                if (map_p && map_p < obj_end) {
                    const char *map_start = strchr(map_p, '{');
                    const char *map_end = map_start ? find_matching_brace(map_start) : NULL;
                    if (map_start && map_end && map_end <= obj_end) {
                        const char *cur = map_start + 1;
                        while (cur < map_end && ps->input_map.count < MAX_INPUT_VARS) {
                            char key[MAX_VAR_NAME] = {0};
                            char val[MAX_VAR_VALUE] = {0};
                            const char *k_start = strchr(cur, '"');
                            if (!k_start || k_start >= map_end) break;
                            const char *k_end = strchr(k_start + 1, '"');
                            if (!k_end || k_end >= map_end) break;
                            size_t k_len = (size_t)(k_end - k_start - 1);
                            if (k_len >= MAX_VAR_NAME) k_len = MAX_VAR_NAME - 1;
                            strncpy(key, k_start + 1, k_len);

                            const char *colon = strchr(k_end, ':');
                            if (!colon || colon >= map_end) break;
                            const char *v_start = strchr(colon, '"');
                            if (v_start && v_start < map_end) {
                                const char *v_end = strchr(v_start + 1, '"');
                                if (!v_end || v_end > map_end) break;
                                size_t v_len = (size_t)(v_end - v_start - 1);
                                if (v_len >= MAX_VAR_VALUE) v_len = MAX_VAR_VALUE - 1;
                                strncpy(val, v_start + 1, v_len);
                                cur = v_end + 1;
                            } else {
                                /* Numeric or raw value */
                                if (sscanf(colon + 1, " %255[^,}\" \t\r\n]", val) == 1) {
                                    cur = colon + 1 + strlen(val);
                                } else {
                                    break;
                                }
                            }
                            if (key[0]) {
                                int entry_idx = ps->input_map.count++;
                                snprintf(ps->input_map.entries[entry_idx].var_name,
                                         sizeof(ps->input_map.entries[entry_idx].var_name),
                                         "%s", key);
                                snprintf(ps->input_map.entries[entry_idx].var_value,
                                         sizeof(ps->input_map.entries[entry_idx].var_value),
                                         "%s", val);
                            }
                        }
                    }
                }

                p = obj_end + 1;
            }
        }
    }
}

int main(int argc, char **argv)
{
    SimulationConfig config;
    memset(&config, 0, sizeof(config));
    config.algorithm = SCHED_HRRN;
    config.rr_quantum = 2;
    config.max_steps = 5000;
    snprintf(config.programs_dir, sizeof(config.programs_dir), "programs");

    bool json_mode = false;
    bool interactive = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--json") == 0) {
            json_mode = true;
        } else if (strcmp(argv[i], "--interactive") == 0) {
            interactive = true;
        }
    }

    if (json_mode) {
        /* Read entire JSON from stdin */
        size_t cap = 65536;
        size_t len = 0;
        char *buffer = (char *)malloc(cap);
        if (!buffer) return 1;

        size_t n;
        while ((n = fread(buffer + len, 1, cap - len - 1, stdin)) > 0) {
            len += n;
            if (len + 1024 >= cap) {
                cap *= 2;
                char *new_buf = (char *)realloc(buffer, cap);
                if (!new_buf) { free(buffer); return 1; }
                buffer = new_buf;
            }
        }
        buffer[len] = '\0';

        parse_json_config(&config, buffer);
        free(buffer);
    } else {
        /* Parse command line flags */
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--scheduler") == 0 && i + 1 < argc) {
                config.algorithm = scheduler_from_name(argv[++i]);
            } else if (strcmp(argv[i], "--quantum") == 0 && i + 1 < argc) {
                config.rr_quantum = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--programs") == 0 && i + 1 < argc) {
                snprintf(config.programs_dir, sizeof(config.programs_dir), "%s", argv[++i]);
            } else if (strcmp(argv[i], "--sandbox") == 0 && i + 1 < argc) {
                snprintf(config.sandbox_dir, sizeof(config.sandbox_dir), "%s", argv[++i]);
            } else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
                config.max_steps = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--process") == 0 && i + 1 < argc) {
                if (config.process_count < MAX_PROCESSES) {
                    char spec[MAX_FILENAME_LEN + 64];
                    snprintf(spec, sizeof(spec), "%s", argv[++i]);
                    char *pid_s = strtok(spec, ":");
                    char *file_s = strtok(NULL, ":");
                    char *arr_s = strtok(NULL, ":");
                    if (pid_s && file_s && arr_s) {
                        int idx = config.process_count++;
                        config.processes[idx].pid = atoi(pid_s);
                        snprintf(config.processes[idx].file,
                                 sizeof(config.processes[idx].file), "%s", file_s);
                        config.processes[idx].arrival_time = atoi(arr_s);
                        config.processes[idx].input_map.count = 0;
                    }
                }
            } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
                /* Format: pid:var=val */
                char spec[MAX_VAR_NAME + MAX_VAR_VALUE + 32];
                snprintf(spec, sizeof(spec), "%s", argv[++i]);
                char *pid_s = strtok(spec, ":");
                char *var_s = strtok(NULL, "=");
                char *val_s = strtok(NULL, "");
                if (pid_s && var_s && val_s) {
                    int pid = atoi(pid_s);
                    for (int p = 0; p < config.process_count; ++p) {
                        if (config.processes[p].pid == pid &&
                            config.processes[p].input_map.count < MAX_INPUT_VARS) {
                            int entry_idx = config.processes[p].input_map.count++;
                            strncpy(config.processes[p].input_map.entries[entry_idx].var_name,
                                    var_s, MAX_VAR_NAME - 1);
                            strncpy(config.processes[p].input_map.entries[entry_idx].var_value,
                                    val_s, MAX_VAR_VALUE - 1);
                            break;
                        }
                    }
                }
            }
        }
    }

    /* Fallback default processes if none provided */
    if (config.process_count == 0) {
        config.process_count = 3;
        config.processes[0].pid = 1;
        snprintf(config.processes[0].file, sizeof(config.processes[0].file), "Program 1.txt");
        config.processes[0].arrival_time = 0;
        config.processes[0].input_map.count = 2;
        strncpy(config.processes[0].input_map.entries[0].var_name, "x", MAX_VAR_NAME);
        strncpy(config.processes[0].input_map.entries[0].var_value, "2", MAX_VAR_VALUE);
        strncpy(config.processes[0].input_map.entries[1].var_name, "y", MAX_VAR_NAME);
        strncpy(config.processes[0].input_map.entries[1].var_value, "7", MAX_VAR_VALUE);

        config.processes[1].pid = 2;
        snprintf(config.processes[1].file, sizeof(config.processes[1].file), "Program_2.txt");
        config.processes[1].arrival_time = 1;
        config.processes[1].input_map.count = 2;
        strncpy(config.processes[1].input_map.entries[0].var_name, "a", MAX_VAR_NAME);
        strncpy(config.processes[1].input_map.entries[0].var_value, "dashboard_output.txt", MAX_VAR_VALUE);
        strncpy(config.processes[1].input_map.entries[1].var_name, "b", MAX_VAR_NAME);
        strncpy(config.processes[1].input_map.entries[1].var_value, "Saved from C simulator", MAX_VAR_VALUE);

        config.processes[2].pid = 3;
        snprintf(config.processes[2].file, sizeof(config.processes[2].file), "Program_3.txt");
        config.processes[2].arrival_time = 4;
        config.processes[2].input_map.count = 1;
        strncpy(config.processes[2].input_map.entries[0].var_name, "a", MAX_VAR_NAME);
        strncpy(config.processes[2].input_map.entries[0].var_value, "dashboard_output.txt", MAX_VAR_VALUE);
    }

    if (interactive && !json_mode) {
        printf("====================================================\n");
        printf("       OPERATING SYSTEM SIMULATOR (C ENGINE)       \n");
        printf("====================================================\n");
        printf("Select Scheduling Algorithm:\n");
        printf("  0 = HRRN (Highest Response Ratio Next)\n");
        printf("  1 = RR   (Round Robin)\n");
        printf("  2 = MLFQ (Multi-Level Feedback Queue)\n");
        printf("> ");
        int choice = 0;
        if (scanf("%d", &choice) == 1) {
            if (choice == 1) config.algorithm = SCHED_RR;
            else if (choice == 2) config.algorithm = SCHED_MLFQ;
            else config.algorithm = SCHED_HRRN;
        }
        if (config.algorithm == SCHED_RR) {
            printf("Enter Round Robin Quantum (e.g. 2): ");
            int q = 2;
            if (scanf("%d", &q) == 1 && q > 0) config.rr_quantum = q;
        }
        printf("\nRunning simulation with %s...\n\n", scheduler_name(config.algorithm));
    }

    Simulation *sim = simulation_create(&config);
    if (!sim) {
        fprintf(stderr, "[Error] Failed to initialize simulation.\n");
        return 1;
    }

    simulation_run(sim);

    /* Output JSON */
    simulation_write_json(sim, stdout);

    simulation_destroy(sim);
    return 0;
}
