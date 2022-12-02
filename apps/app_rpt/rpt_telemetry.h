
void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink);

/*
 * Parse a request METER request for telemetry thread
 * This is passed in a comma separated list of items from the function table entry
 * There should be 3 or 4 fields in the function table entry: device, channel, meter face, and  optionally: filter
 */

int handle_meter_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args);

/*! \brief Playback a meter reading */
int function_meter(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Set or reset a USER Output bit */
int function_userout(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Execute shell command */
int function_cmd(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

void flush_telem(struct rpt *myrpt);

/*! \brief Routine that hangs up all links and frees all threads related to them hence taking a "bird bath".  Makes a lot of noise/cleans up the mess */
void birdbath(struct rpt *myrpt);

/*! \note must be called locked */
void cancel_pfxtone(struct rpt *myrpt);
