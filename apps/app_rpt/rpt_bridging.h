
enum rpt_chan_name {
	RPT_RXCHAN, /* Receive channel */
	RPT_TXCHAN, /* Transmit channel */
	RPT_PCHAN,
	RPT_LOCALTXCHAN,
	RPT_MONCHAN, /* Monitor channel */
	RPT_TXPCHAN,
};

enum rpt_chan_type {
	RPT_LOCAL,
	RPT_TELEMETRY,
	RPT_MONITOR,
};

/* Each of these corresponds to a member of the rpt_conf structure in app_rpt.h */
enum rpt_conf_type {
	RPT_CONF,	/* Audio on all links */
	RPT_TXCONF, /* Local Audio */
};

#define RPT_TXCONF_NAME "TXCONF" /* TX Conference Name */
#define RPT_CONF_NAME "CONF"	 /* Repeater Conference Name */
#define RPT_CONF_NAME_SIZE 10
enum rpt_chan_flags {
	RPT_LINK_CHAN = (1 << 0),
};

/*!
 * \brief Hang up an Asterisk channel belonging to a repeater
 * \param myrpt
 * \param chantype
 * \note Do not call if the channel does not exist (safe, but will emit a warning)
 */
void rpt_hangup(struct rpt *myrpt, enum rpt_chan_name chantype);

/*!
 * \brief Request a repeater channel
 * \param data rpt or rpt_link structure
 * \param chantype
 * \param flags
 * \note myrpt->lock must be held when calling
 * \retval 0 on success, -1 on failure
 */
int __rpt_request(void *data, struct ast_format_cap *cap, enum rpt_chan_name chantype, enum rpt_chan_flags flags);

#define rpt_request(data, cap, chantype) __rpt_request(data, cap, chantype, 0)

/*!
 * \brief Request a pseudo channel
 * \param cap
 * \return channel on success
 * \return NULL on failure
 */
struct ast_channel *__rpt_request_local_chan(struct ast_format_cap *cap, const char *exten, enum rpt_chan_type type);

#define rpt_request_local_chan(cap, exten) __rpt_request_local_chan(cap, exten, RPT_LOCAL)
#define rpt_request_telem_chan(cap, exten) __rpt_request_local_chan(cap, exten, RPT_TELEMETRY)
#define rpt_request_mon_chan(cap, exten) __rpt_request_local_chan(cap, exten, RPT_MONITOR)

/*!
 * \brief Request a repeater channel not associated with a real device
 * \param myrpt
 * \param chantype
 * \param flags
 * \note myrpt->lock must be held when calling
 * \retval 0 on success, -1 on failure
 */
int __rpt_request_local(void *data, struct ast_format_cap *cap, enum rpt_chan_name chantype, enum rpt_chan_flags flags, const char *exten);

#define rpt_request_local(data, cap, chantype, exten) __rpt_request_local(data, cap, chantype, 0, exten)

int __rpt_conf_create(struct rpt *myrpt, enum rpt_conf_type type, const char *file, int line);

int __rpt_conf_add(struct ast_channel *chan, struct rpt *myrpt, enum rpt_conf_type type, const char *file, int line);

#define rpt_conf_create(myrpt, type) __rpt_conf_create(myrpt, type, __FILE__, __LINE__)
#define rpt_conf_add(chan, myrpt, type) __rpt_conf_add(chan, myrpt, type, __FILE__, __LINE__)

/*!
 * \param chan Channel to play tone on
 * \param tone tone type (e.g., "dial", "congestion")
 * \retval 0 on success, -1 on failure
 */
int rpt_play_tone(struct ast_channel *chan, const char *tone);

/*!
 * \brief Play congestion on a channel
 * \param chan
 * \retval 0 on success, -1 on failure
 */
#define rpt_play_congestion(chan) rpt_play_tone(chan, "congestion")

/*!
 * \brief Play dialtone on a channel
 * \param chan
 * \retval 0 on success, -1 on failure
 */
#define rpt_play_dialtone(chan) rpt_play_tone(chan, "dial")

/*!
 * \brief Get if channel is muted in conference
 * \param chan
 * \param myrpt
 * \retval 0 if not muted, 1 if muted
 */
int rpt_conf_get_muted(struct ast_channel *chan, struct rpt *myrpt);

/*!
 * \brief Stop playing tones on a channel
 * \param chan
 * \retval 0 on success, -1 on failure
 */
int rpt_stop_tone(struct ast_channel *chan);

/*!
 * \brief Set the tone zone on a channel
 * \param chan
 * \retval -1 on failure, 0 on success
 */
int rpt_set_tone_zone(struct ast_channel *chan, const char *tz);

#define DEFAULT_TALKING_THRESHOLD 160

/*!
 * \brief Get value of rxisoffhook
 * \note Only use with DAHDI channels!
 * \param chan
 * \retval -1 on failure
 * \retval 0 if on hook, 1 if off hook
 */
int dahdi_rx_offhook(struct ast_channel *chan);

/*!
 * \brief Set on/off hook state
 * \note Only use with DAHDI channels!
 * \param chan
 * \param offhook 1 for off hook, 0 for on hook
 * \retval -1 on failure
 * \retval 0 if on hook, 1 if off hook
 */
int dahdi_set_hook(struct ast_channel *chan, int offhook);

#define dahdi_set_offhook(chan) dahdi_set_hook(chan, 1)
#define dahdi_set_onhook(chan) dahdi_set_hook(chan, 0)

/*!
 * \brief Set echo cancellation on DAHDI channel
 * \param chan
 * \param ec 0 to disable, non-zero to enable
 * \retval 0 on success, -1 on failure
 */
int dahdi_set_echocancel(struct ast_channel *chan, int ec);