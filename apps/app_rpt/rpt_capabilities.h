

/* Return 1 if a web transceiver node */
int iswebtransceiver(struct rpt_link *l);

/*
* Return 1 if rig is multimode capable
*/
int multimode_capable(struct rpt *myrpt);

/*
* Return 1 if rig is narrow capable
*/
int narrow_capable(struct rpt *myrpt);

char is_paging(struct rpt *myrpt);
