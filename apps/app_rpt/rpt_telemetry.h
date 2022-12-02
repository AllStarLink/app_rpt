
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

/*! Send telemetry tones */
int send_tone_telemetry(struct ast_channel *chan, char *tonestring);

int telem_any(struct rpt *myrpt, struct ast_channel *chan, char *entry);

/*! \brief This function looks up a telemetry name in the config file, and does a telemetry response as configured.
 * 4 types of telemtry are handled: Morse ID, Morse Message, Tone Sequence, and a File containing a recording. */
int telem_lookup(struct rpt *myrpt, struct ast_channel *chan, char *node, char *name);

/*! \brief Routine to process various telemetry commands that are in the myrpt structure
 * Used extensively when links and built/torn down and other events are processed by the rpt_master threads. */
void handle_varcmd_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *varcmd);

void *rpt_tele_thread(void *this);

/*! \brief More repeater telemetry routines. */
void rpt_telemetry(struct rpt *myrpt, int mode, void *data);
