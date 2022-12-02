
/*! \brief Retrieve an int from a config file */
int retrieve_astcfgint(struct rpt *myrpt, char *category, char *name, int min, int max, int defl);

/*! \brief Retrieve a wait interval */
int get_wait_interval(struct rpt *myrpt, int type);
