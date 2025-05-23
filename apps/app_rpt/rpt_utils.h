
#define	DELIMCHR ','
#define	QUOTECHR 34

/*
* Match a keyword in a list, and return index of string plus 1 if there was a match,* else return 0.
* If param is passed in non-null, then it will be set to the first character past the match
*/

int matchkeyword(char *string, char **param, char *keywords[]);

/*!
 * \brief Explode a string into an array of pointers to the start of each token.
 * \param str The string to explode (will be modified)
 * \param strp An array of pointers to the start of each token + 1 or more for a NULL end token
 * \param limit The maximum number of tokens to find + 1 or more for the NULL end token
 * \param delim The delimiter to use
 * \param quote The quote character to use
 * \return The number of substrings found.
 */
int explode_string(char *str, char *strp[], size_t limit, char delim, char quote);

char *strupr(char *instr);

char *string_toupper(char *str);

/*!
 * \brief Break up a delimited string into a table of substrings
 * \param str - delimited string (will be modified)
 * \param strp- An array of pointers to the substrings (in the modified "str"), NULL will be placed at end of list
 * \param limit- maximum number of substrings to process
 * \return The number of substrings found
 */
int finddelim(char *str, char *strp[], size_t limit);

/*
* Skip characters in string which are in charlist, and return a pointer to the
* first non-matching character
*/

char *skipchars(char *string, char *charlist);

/*
 * Return a pointer to the first non-whitespace character
 */

char *eatwhite(char *s);

int myatoi(const char *str);

/*! \brief Convert decimals of frequency to int */
int decimals2int(char *fraction);

/*! \brief Split frequency into mhz and decimals */
int split_freq(char *mhz, char *decimals, char *freq);

int mycompar(const void *a, const void *b);

long diskavail(struct rpt *myrpt);

void rpt_localtime(time_t * t, struct ast_tm *lt, const char *tz);

time_t rpt_mktime(struct ast_tm *tm, const char *zone);

/*!
 * \brief Get system monotonic 
 * This returns the CLOCK_MONOTONIC time
 * \retval		Monotonic seconds.
 */
time_t rpt_time_monotonic(void);

/*! \brief Append a command to the macro buffer
 * \param myrpt Pointer to the rpt structure
 * \param cmd Command to append
 * \retval 0 on success, -1 on failure (result of ast_str_append)
 */
int macro_append(struct rpt *myrpt, const char *cmd);

/*! \brief Do timer value update, limit to end_val
 * \note Timers can end at any value and will stop and maintain the end value.
 * If the timer value is set to 0, it will maintain 0 and not reset to the end
 * value.  This behavior allows for a timer to reach an value then be set to 0
 * to indicate some code has been executed (a oneshot).
 */
void update_timer(int *timer_ptr, int elap, int end_val);

/*! \brief Initialize a text frame */
void init_text_frame(struct ast_frame *wf, const char *src);
