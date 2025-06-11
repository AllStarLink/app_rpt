
/*! \brief Retrieve an int from a config file */
int retrieve_astcfgint(struct rpt *myrpt, const char *category, const char *name, int min, int max, int defl);

/*! \brief Retrieve a wait interval */
int get_wait_interval(struct rpt *myrpt, enum rpt_delay type);

/*!
 * \brief Retrieve a memory channel
 * \retval 0 if successful
 * \retval -1 if channel not found
 * \retval 1 if parse error
 */
int retrieve_memory(struct rpt *myrpt, char *memory);

/*! \brief retrieve memory setting and set radio */
int get_mem_set(struct rpt *myrpt, char *digitbuf);

/*! \brief Process DTMF keys passed */
void local_dtmfkey_helper(struct rpt *myrpt, char c);

/*!
 * \brief Query echolink channel for a node's callsign
 * \param	node		pointer to node to lookup
 * \param	callsign	pointer to buffer to hold callsign
 * \param	callsignlen	length of callsign buffer
 * \retval 0 if successful
 * \retval -1 if not successful
 */
int elink_query_callsign(char *node, char *callsign, int callsignlen);

/*!
 * \brief Query the link box channel to see if node exists
 * \param	node		pointer to node to lookup
 * \retval 1 if node exists
 * \retval 0 if node does not exist
 */
int tlb_query_node_exists(const char *node);

/*!
 * \brief Query the link box channel for a node's callsign
 * \param	node		pointer to node to lookup
 * \param	callsign	pointer to buffer to hold callsign
 * \param	callsignlen	length of callsign buffer
 * \retval 0 if successful
 * \retval -1 if not successful
 */
int tlb_query_callsign(const char *node, char *callsign, int callsignlen);

/*!
 * \brief Node lookup function.  This function will take the nodelist that has been read into memory
 * and try to match the node number that was passed to it.  If it is found, the function requested will succeed.
 * If not, it will fail.  Called when a connection to a remote node is requested.
 * \param  myrpt		Calling repeater structure
 * \param  digitbuf		The node number of match
 * \param  nodedata		A buffer to hold the matching node information
 * \param  nodedatalength	The length of the str buffer
 * \param  wilds		Set to 1 to perform a wild card lookup
 * \retval -1 			If not successful
 * \retval 0 			If successful
 */
int node_lookup(struct rpt *myrpt, char *digitbuf, char *nodedata, size_t nodedatalength, int wilds);

/*!
 * \brief Forward node lookup function.  This function will take the nodelist 
 * and try to match the node number that was passed to it.  If it is found, the function requested will succeed.
 * If not, it will fail.  Called when a connection to a remote node is requested.
 * \param  digitbuf		The node number of match
 * \param  cfg			Asterisk configuration file pointer
 * \param  nodedata		A buffer to hold the matching node information
 * \param  nodedatalength	The length of the str buffer
 * \retval -1 			If not successful
 * \retval 0 			If successful
 */

int forward_node_lookup(char *digitbuf, struct ast_config *cfg, char *nodedata, size_t nodedatalength);

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

/*! \brief Update boolean values used in currently referenced rpt structure */
void rpt_update_boolean(struct rpt *myrpt, char *varname, int newval);

/*! \brief Test strings for valid DNS contents */
int rpt_is_valid_dns_name(const char *dns_name);
