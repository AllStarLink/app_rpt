
/*! \file
 *
 * \brief RPT link functions
 */

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

/*!
 * \brief Add an rpt_link to a rpt
 * \param myrpt
 * \param l Link to insert into the repeater's linked list of links
 */
void rpt_link_add(struct ao2_container *links, struct rpt_link *l);

/*!
 * \brief Remove an rpt_link from a links container
 * \param links ao2_container to remove the link from
 * \param l Link to remove from the container
 */

void rpt_link_remove(struct ao2_container *links, struct rpt_link *l);

/*!
 * \brief destroy ao2 object
 * \param obj rpt_link object called by the ao2_alloc() destructor function
 */
void rpt_link_destroy(void *obj);

/*!
 * \brief __mklinklist() flags
 */
enum __mklinklist_flags {
	/*! \brief Create RPT_LINK format (<mode><node>) string */
	USE_FORMAT_RPT_LINK = 0,
	/*! \brief Create RPT_ALINK format (<node><mode><keystate>) string */
	USE_FORMAT_RPT_ALINK = (1 << 0),
	/*! \brief Create RPT_LINKPOST format (<mode><node>) string */
	USE_FORMAT_RPT_LINKPOST = (1 << 1),
	/*! \brief Limit string length to avoid fragmentation */
	LIMIT_STRING_LENGTH = (1 << 8),
};

/*!
 * \brief Create a list of links for this node.
 * Must be called locked.
 * \param myrpt		Pointer to rpt structure.
 * \param mylink	Pointer to rpt_link structure.
 * \param buf		Pointer to ast_str buffer - link string is generated in this buffer.
 * \param flags		Flags specifying format as the returned string.
 * format is returned. \retval		link count.
 */

int __mklinklist(struct rpt *myrpt, struct rpt_link *mylink, struct ast_str **buf, enum __mklinklist_flags flags);

/*! \brief must be called locked */
void __kickshort(struct rpt *myrpt);

/*! \brief Updates the active links (channels) list that that the repeater has */
void rpt_update_links(struct rpt *myrpt);

/*! \brief Free link and associated internal memory.
 * \param link Link structure to free
 */
void rpt_link_free(struct rpt_link *link);

/*! \brief Structure used to share data with connect_data thread */
struct rpt_connect_data {
	struct rpt *myrpt;
	char *digitbuf; /* Node number in string format. Thread takes ownership and frees. */
	enum link_mode mode;
	unsigned int perma:1; /* permanent link */
	enum rpt_command_source command_source;
	struct rpt_link *mylink; /* Must remain valid for thread lifetime or be ref-counted. */
};

/*!
 * Thread entry point for establishing a link connection.
 * \brief Connect a link
 * \param data Pointer to rpt_connect_data structure. Thread takes ownership and frees.
 * \return NULL on success, or an error indicator (implementation-specific)
 * \note Intended for use with pthread_create or similar threading APIs.
 */
void *rpt_link_connect(void *data);
