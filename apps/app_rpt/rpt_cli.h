
char *res2cli(int r);
char *handle_cli_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_dump(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_nodes(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_xnode(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_local_nodes(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_lstats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_restart(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_fun(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_playback(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_fun1(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_setvar(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_showvars(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_lookup(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_localplay(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_sendall(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_sendtext(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *handle_cli_page(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

/* Debug mode */
/*
* Enable or disable debug output at a given level at the console
*/
int rpt_do_debug(int fd, int argc, const char * const *argv);

/*
* Dump rpt struct debugging onto console
*/
int rpt_do_dump(int fd, int argc, const char * const *argv);

/*
* Dump statistics onto console
*/
int rpt_do_stats(int fd, int argc, const char * const *argv);

/*
* Link stats function
*/
int rpt_do_lstats(int fd, int argc, const char * const *argv);

/*
* List all nodes connected, directly or indirectly
*/
int rpt_do_nodes(int fd, int argc, const char * const *argv);
int rpt_do_xnode(int fd, int argc, const char * const *argv);

/*
* List all locally configured nodes
*/
int rpt_do_local_nodes(int fd, int argc, const char * const *argv);

/*
* reload vars 
*/
int rpt_do_reload(int fd, int argc, const char * const *argv);

/*
* restart app_rpt
*/
int rpt_do_restart(int fd, int argc, const char * const *argv);

/*
* send an Audio File from the CLI
*/
int rpt_do_playback(int fd, int argc, const char * const *argv);
int rpt_do_localplay(int fd, int argc, const char * const *argv);
/* Send to all nodes */
int rpt_do_sendall(int fd, int argc, const char * const *argv);
int rpt_do_sendall2(int fd, int argc, char *argv[]);
int rpt_do_sendtext(int fd, int argc, const char * const *argv);

/*
* send an app_rpt DTMF function from the CLI
*/   
int rpt_do_fun(int fd, int argc, const char * const *argv);

/*
	allows us to test rpt() application data commands
*/
int rpt_do_fun1(int fd, int argc, const char * const *argv);

/*
* send an app_rpt **command** from the CLI
*/
int rpt_do_cmd(int fd, int argc, const char * const *argv);

/*
* set a node's main channel variable from the command line 
*/
int rpt_do_setvar(int fd, int argc, const char * const *argv);

/*
* Display a node's main channel variables from the command line 
*/
int rpt_do_showvars(int fd, int argc, const char * const *argv);
/* Paging function */
int rpt_do_page(int fd, int argc, const char * const *argv);
int rpt_do_lookup(int fd, int argc, const char * const *argv);
