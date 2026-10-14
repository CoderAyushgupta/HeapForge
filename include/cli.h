/* cli.h - interactive shell and one-shot command dispatcher. */
#ifndef HF_CLI_H
#define HF_CLI_H

/* Run one command line (e.g. "malloc 256"). Returns 0 ok, >0 error, -1 quit. */
int hf_cli_execute(const char *line);
/* True if `name` is a known command word. */
int hf_cli_is_command(const char *name);
/* Interactive read-eval-print loop on stdin. Returns the last status. */
int hf_cli_repl(void);
/* Run "a; b; c" sequentially, echoing each command. */
int hf_cli_script(const char *commands);
void hf_cli_usage(void);

#endif
