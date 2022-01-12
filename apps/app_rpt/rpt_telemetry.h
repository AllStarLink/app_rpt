
/*
 * Parse a request METER request for telemetry thread
 * This is passed in a comma separated list of items from the function table entry
 * There should be 3 or 4 fields in the function table entry: device, channel, meter face, and  optionally: filter
 */
int handle_meter_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args);

/*
 * Handle USEROUT telemetry
 */

int handle_userout_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args);

int function_meter(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
int function_userout(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
void flush_telem(struct rpt *myrpt);
void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink);

/* place an ID event in the telemetry queue */
void queue_id(struct rpt *myrpt);

/*
* This function looks up a telemetry name in the config file, and does a telemetry response as configured.
*
* 4 types of telemtry are handled: Morse ID, Morse Message, Tone Sequence, and a File containing a recording.
*/
int telem_lookup(struct rpt *myrpt,struct ast_channel *chan, char *node, char *name);
