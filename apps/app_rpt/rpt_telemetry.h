#define TELEM_DATASTORE "telemetry"

void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink);

void flush_telem(struct rpt *myrpt);

/*! \brief Routine that hangs up all links and frees all threads related to them hence taking a "bird bath".  Makes a lot of noise/cleans up the mess */
void birdbath(struct rpt *myrpt);

/*! \note must be called locked */
void cancel_pfxtone(struct rpt *myrpt);

void *rpt_tele_thread(void *this);

/*! \brief More repeater telemetry routines. */
void rpt_telemetry(struct rpt *myrpt, int mode, void *data);

/*!
 * \brief Read time_t from telemetry datastore
 * \param chan The channel to read from
 * \param cmd The command to execute
 * \param data Pointer to the data buffer
 * \param buf Buffer to store the result
 * \param len Length of the buffer
 * \return 0 on success, -1 on failure
 */
int rpt_telem_read_datastore(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len);
