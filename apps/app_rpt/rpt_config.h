
/*! \brief Retrieve an int from a config file */
int retrieve_astcfgint(struct rpt *myrpt, char *category, char *name, int min, int max, int defl);

/*! \brief Retrieve a wait interval */
int get_wait_interval(struct rpt *myrpt, int type);

/*!
 * \brief Retrieve a memory channel
 * \retval 0 if successful
 * \retval -1 if channel not found
 * \retval 1 if parse error
 */
int retrieve_memory(struct rpt *myrpt, char *memory);

/*! \brief Process DTMF keys passed */
void local_dtmfkey_helper(struct rpt *myrpt, char c);

int elink_db_get(char *lookup, char c, char *nodenum, char *callsign, char *ipaddr);

int tlb_node_get(char *lookup, char c, char *nodenum, char *callsign, char *ipaddr, char *port);

/*!
 * \brief AllStar Network node lookup function.  This function will take the nodelist that has been read into memory
 * and try to match the node number that was passed to it.  If it is found, the function requested will succeed.
 * If not, it will fail.  Called when a connection to a remote node is requested.
 */
int node_lookup(struct rpt *myrpt, char *digitbuf, char *str, int strmax, int wilds);

char *forward_node_lookup(struct rpt *myrpt, char *digitbuf, struct ast_config *cfg);

/*! 
 * \brief This is the initialization function.  This routine takes the data in rpt.conf and setup up the variables needed for each of
 * the repeaters that it finds.  There is some minor sanity checking done on the data passed, but not much.
 * 
 * \note This is kind of a mess to read.  It uses the asterisk native function to read config files and pass back values assigned to keywords.
 */
void load_rpt_vars(int n, int init);

/*! \note the convention is that macros in the data from the rpt( application
 * are all at the end of the data, separated by the | and start with a *
 * when put into the macro buffer, the characters have their high bit
 * set so the macro processor knows they came from the application data
 * and to use the alt-functions table.
 * sph: */
int rpt_push_alt_macro(struct rpt *myrpt, char *sptr);
