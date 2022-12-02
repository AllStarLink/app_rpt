
/*! \note Called once in app_rpt.c, so exposed as non-static */
int rpt_do_sendall(int fd, int argc, const char *const *argv);

int rpt_cli_load(void);
int rpt_cli_unload(void);
