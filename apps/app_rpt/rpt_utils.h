
#define	DELIMCHR ','
#define	QUOTECHR 34

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

char *strupr(char *instr);

char *string_toupper(char *str);

/*
* Break up a delimited string into a table of substrings
*
* str - delimited string ( will be modified )
* strp- list of pointers to substrings (this is built by this function), NULL will be placed at end of list
* limit- maximum number of substrings to process
*/
int finddelim(char *str, char *strp[], int limit);

/*
* Skip characters in string which are in charlist, and return a pointer to the
* first non-matching character
*/

char *skipchars(char *string, char *charlist);

/*
 * Return a pointer to the first non-whitespace character
 */

char *eatwhite(char *s);

int myatoi(char *str);

/*! \brief Convert decimals of frequency to int */
int decimals2int(char *fraction);

/*! \brief Split frequency into mhz and decimals */
int split_freq(char *mhz, char *decimals, char *freq);

int mycompar(const void *a, const void *b);

long diskavail(struct rpt *myrpt);

void rpt_localtime(time_t * t, struct ast_tm *lt, char *tz);

time_t rpt_mktime(struct ast_tm *tm, char *zone);
