
/*! \file
 *
 * \brief RPT link functions
 */

void init_linkmode(struct rpt *myrpt, struct rpt_link *mylink, int linktype);

void set_linkmode(struct rpt_link *mylink, int linkmode);

int altlink(struct rpt *myrpt, struct rpt_link *mylink);

int altlink1(struct rpt *myrpt, struct rpt_link *mylink);

void rpt_qwrite(struct rpt_link *l, struct ast_frame *f);

int linkcount(struct rpt *myrpt);

/*! \brief Considers repeater received RSSI and all voter link RSSI information and set values in myrpt structure. */
int FindBestRssi(struct rpt *myrpt);
