/* Built-in completers: files, make targets/variables, $PATH commands,
 * exec output; flag values seen on the line. */
#ifndef AUTOCOMPLETE_BUILTINS_H
#define AUTOCOMPLETE_BUILTINS_H

/* flag values seen on the line, e.g. make -C dir */
#define MAXSEEN 64
extern const char *g_seen_flag[MAXSEEN], *g_seen_val[MAXSEEN];
extern int g_nseen;

/* Run shell and emit one candidate per line ("name<TAB>desc"); tree: char to
 * fold lines at past g_cur (0 = none). */
void emit_cmd_output(const char *shell, const char *suffix, int space, char tree);
/* Files (dirs always, other files only if they match one of the
 * space-separated fnmatch patterns in globs, when given). */
void complete_files(int dirs_only, const char *globs);
/* complete_files, but "~ro" lists "~user/" names first. */
void complete_paths(int dirs_only, const char *globs);
void complete_users(void);
void complete_signals(void);
void complete_hosts(void);
void complete_pids(void);
void complete_processes(void);
/* Delegate to a cobra program's `__complete` (words[0..n-1], last = current). */
void complete_cobra(char **words, int n);
void complete_make_targets(void);
void complete_make_variables(void);
/* Executables on $PATH (a word with '/' completes as a path). */
void complete_commands(void);

#endif
