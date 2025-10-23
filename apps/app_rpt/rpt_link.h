
/*! \file
 *
 * \brief RPT link functions
 */

/*!
* \brief Loops over (traverses) the entries in an AO2 container list.
* \param container This is a pointer to the AO2 container
* \param var This is the name of the variable that will hold a pointer to the
* current list entry on each iteration. It must be declared before calling
* this macro.
* \param iterator This is the name of the variable that will be used for
* the AO2_ITERATOR. It must be declared before calling this macro.
*
* This macro is use to loop over (traverse) the entries in an AO2 container list. It uses a
* \a for loop, and supplies the enclosed code with a pointer to each list
* entry as it loops. It is typically used as follows:
* \code
* static ao2_container container;
* ao2_iterator_t iterator;
* ...
* struct list_entry {
*    ...
* }
* ...
* struct list_entry *current;
* ...
* RPT_AO2_LIST_TRAVERSE(&container, current, iterator) {
	(do something with current here)
* }
* ao2_iterator_destroy(&iterator);
* \endcode

*/

#define RPT_AO2_LIST_TRAVERSE(container, var, iterator) \
	(iterator) = ao2_iterator_init((container), 0); \
	for (; ((var) = ao2_iterator_next(&(iterator))); ao2_ref((var), -1))

void init_linkmode(struct rpt *myrpt, struct rpt_link *mylink, int linktype);

void set_linkmode(struct rpt_link *mylink, enum rpt_linkmode linkmode);

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
void FindBestRssi(struct rpt *myrpt);

void do_dtmf_phone(struct rpt *myrpt, struct rpt_link *mylink, char c);

/*! \brief Send rx rssi out on all links. */
void rssi_send(struct rpt *myrpt);

void send_link_dtmf(struct rpt *myrpt, char c);

void send_link_keyquery(struct rpt *myrpt);

/*! 1Code has comments. Press enter to view.
 * \brief Add an rpt_link to a rpt
 * \param myrpt
 * \param l Link to insert into the repeater's linked list of links
 */
void rpt_link_add(struct ao2_container *links, struct rpt_link *l);

/*!
 * \brief Remove an rpt_link from a rpt
 * \param myrpt
 * \param l Link to remove from the repeater's linked list of links
 */
void rpt_link_remove(struct ao2_container *links, struct rpt_link *l);

/*!
 * \brief Create a list of links for this node.
 * Must be called locked.
 * \param myrpt		Pointer to rpt structure.
 * \param mylink	Pointer to rpt_link structure.
 * \param buf		Pointer to ast_str buffer - link string is generated in this buffer.
 * \param alink_format	Flag to indicate if RPT_ALINK format is returned. If not, RPT_LINK
 * format is returned. \retval		link count.
 */

int __mklinklist(struct rpt *myrpt, struct rpt_link *mylink, struct ast_str **buf, int alink_format);

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
int connect_link(struct rpt *myrpt, char *node, enum link_mode mode, int perma);

/*! \brief destroy ao2 object
 */
void rpt_link_destroy(void *obj);
