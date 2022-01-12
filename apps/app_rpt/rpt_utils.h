

/* node logging function */
void donodelog(struct rpt *myrpt,char *str);

/* must be called locked */
void __mklinklist(struct rpt *myrpt, struct rpt_link *mylink, char *buf,int flag);

int node_lookup(struct rpt *myrpt,char *digitbuf,char *str, int strmax, int wilds);
char *forward_node_lookup(struct rpt *myrpt,char *digitbuf, struct ast_config *cfg);

/*
	the convention is that macros in the data from the rpt( application
	are all at the end of the data, separated by the | and start with a *
	when put into the macro buffer, the characters have their high bit
	set so the macro processor knows they came from the application data
	and to use the alt-functions table.
	sph:
*/
int rpt_push_alt_macro(struct rpt *myrpt, char *sptr);

/*
* Match a keyword in a list, and return index of string plus 1 if there was a match,* else return 0.
* If param is passed in non-null, then it will be set to the first character past the match
*/
int matchkeyword(char *string, char **param, char *keywords[]);

/*
* Break up a delimited string into a table of substrings
*
* str - delimited string ( will be modified )
* strp- list of pointers to substrings (this is built by this function), NULL will be placed at end of list
* limit- maximum number of substrings to process
* delim- user specified delimeter
* quote- user specified quote for escaping a substring. Set to zero to escape nothing.
*
* Note: This modifies the string str, be suer to save an intact copy if you need it later.
*
* Returns number of substrings found.
*/
int explode_string(char *str, char *strp[], int limit, char delim, char quote);
char *strupr(char *str);

/*
* Break up a delimited string into a table of substrings
*
* str - delimited string ( will be modified )
* strp- list of pointers to substrings (this is built by this function), NULL will be placed at end of list
* limit- maximum number of substrings to process
*/
int finddelim(char *str, char *strp[], int limit);
char *string_toupper(char *str);

/*
* Skip characters in string which are in charlist, and return a pointer to the
* first non-matching character
*/
char *skipchars(char *string, char *charlist);

int myatoi(char *str);
int mycompar(const void *a, const void *b);
int topcompar(const void *a, const void *b);

/*
 * Return a pointer to the first non-whitespace character
 */
char *eatwhite(char *s);

long diskavail(struct rpt *myrpt);

void rpt_localtime( time_t *t, struct ast_tm *lt, char *tz);
time_t rpt_mktime(struct ast_tm *tm,char *zone);

/*
 * Translate function
 */
char func_xlat(struct rpt *myrpt,char c,struct rpt_xlat *xlat);

/* Return 1 if a web transceiver node */
int iswebtransceiver(struct  rpt_link *l);

int function_cmd(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*
* Retrieve a wait interval
*/
int get_wait_interval(struct rpt *myrpt, int type);

/*
* Wait a configurable interval of time 
*/
int wait_interval(struct rpt *myrpt, int type, struct ast_channel *chan);

/* Retrieve an int from a config file */
int retrieve_astcfgint(struct rpt *myrpt,char *category, char *name, int min, int max, int defl);

int elink_db_get(char *lookup, char c, char *nodenum,char *callsign, char *ipaddr);
int tlb_node_get(char *lookup, char c, char *nodenum,char *callsign, char *ipaddr, char *port);

int morse_cat(char *str, int freq, int duration);

/*
	retrieve memory setting and set radio
*/
int get_mem_set(struct rpt *myrpt, char *digitbuf);

/*
 * Retrieve a memory channel
 * Return 0 if sucessful,
 * -1 if channel not found,
 *  1 if parse error
 */
int retrieve_memory(struct rpt *myrpt, char *memory);

/*
	steer the radio selected channel to either one programmed into the radio
	or if the radio is VFO agile, to an rpt.conf memory location.
*/
int channel_revert(struct rpt *myrpt);

int channel_steer(struct rpt *myrpt, char *data);

/*
* Split frequency into mhz and decimals
*/
int split_freq(char *mhz, char *decimals, char *freq);

/*
* Split ctcss frequency into hertz and decimal
*/
int split_ctcss_freq(char *hertz, char *decimal, char *freq);

/*
 * Convert decimals of frequency to int
 */

int decimals2int(char *fraction);

char is_paging(struct rpt *myrpt);
