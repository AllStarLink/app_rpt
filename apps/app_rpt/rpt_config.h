
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
