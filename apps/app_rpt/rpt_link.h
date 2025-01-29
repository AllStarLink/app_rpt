
/*! \file
 *
 * \brief RPT link functions
 */

void init_linkmode(struct rpt *myrpt, struct rpt_link *mylink, int linktype);

void set_linkmode(struct rpt_link *mylink, int linkmode);

int altlink(struct rpt *myrpt, struct rpt_link *mylink);

/*!
 * \brief Add an rpt_tele to a rpt
 * \param myrpt
 * \param t Telemetry to insert into the repeater's linked list of telemetries
 */
void tele_link_add(struct rpt *myrpt, struct rpt_tele *t);

/*!
 * \brief Remove an rpt_tele from a rpt
 * \param myrpt
 * \param t Telemetry to remove from the repeater's linked list of telemetries
 */
void tele_link_remove(struct rpt *myrpt, struct rpt_tele *t);

int altlink1(struct rpt *myrpt, struct rpt_link *mylink);

void rpt_qwrite(struct rpt_link *l, struct ast_frame *f);

int linkcount(struct rpt *myrpt);

/*! \brief Considers repeater received RSSI and all voter link RSSI information and set values in myrpt structure. */
int FindBestRssi(struct rpt *myrpt);

void do_dtmf_phone(struct rpt *myrpt, struct rpt_link *mylink, char c);

/*! \brief Send rx rssi out on all links. */
void rssi_send(struct rpt *myrpt);

void send_link_dtmf(struct rpt *myrpt, char c);

void send_link_keyquery(struct rpt *myrpt);

void send_tele_link(struct rpt *myrpt, char *cmd);

/*!
 * \brief Add an rpt_link to a rpt
 * \param myrpt
 * \param l Link to insert into the repeater's linked list of links
 */
void rpt_link_add(struct rpt *myrpt, struct rpt_link *l);

/*!
 * \brief Remove an rpt_link from a rpt
 * \param myrpt
 * \param l Link to remove from the repeater's linked list of links
 */
void rpt_link_remove(struct rpt *myrpt, struct rpt_link *l);

/*! \brief must be called locked */
int __mklinklist(struct rpt *myrpt, struct rpt_link *mylink, char *buf, size_t bufsize, int flag);

/*! \brief must be called locked */
int __get_nodelist_size (struct rpt *myrpt);

/*! \brief must be called locked */
void __kickshort(struct rpt *myrpt);

/*! \brief Updates the active links (channels) list that that the repeater has */
void rpt_update_links(struct rpt *myrpt);

/*! 
 * \brief Connect a link 
 * \retval -2 Attempt to connect to self 
 * \retval -1 No such node
 * \retval 0 Success
 * \retval 1 No match yet
 * \retval 2 Already connected to this node
 */
int connect_link(struct rpt *myrpt, char *node, int mode, int perma);
