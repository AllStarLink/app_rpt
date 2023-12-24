
enum rpt_chan_type {
	RPT_RXCHAN,	/* Receive channel */
	RPT_TXCHAN,	/* Transmit channel */
	RPT_PCHAN,
	RPT_DAHDITXCHAN,
	RPT_MONCHAN,	/* Monitor channel */
	RPT_PARROTCHAN,
	RPT_TELECHAN,
	RPT_BTELECHAN,
	RPT_VOXCHAN,
	RPT_TXPCHAN,
};

/* Each of these corresponds to a member of the rpt_conf structure in app_rpt.h */
enum rpt_conf_type {
	RPT_CONF,
	RPT_TXCONF,
	RPT_TELECONF,
};

/* Uses same flag name style as DAHDI_CONF flags, since that's what these are based on */
enum rpt_conf_flags {
	RPT_CONF_NORMAL = (1 << 0),
	RPT_CONF_MONITOR = (1 << 1),
	RPT_CONF_MONITORTX = (1 << 2),
	RPT_CONF_CONF = (1 << 3),
	RPT_CONF_CONFANN = (1 << 4),
	RPT_CONF_CONFMON = (1 << 5),
	RPT_CONF_CONFANNMON = (1 << 6),
	RPT_CONF_LISTENER = (1 << 7),
	RPT_CONF_TALKER = (1 << 8),
};

enum rpt_chan_flags {
	RPT_LINK_CHAN = (1 << 0),
};

/*!
 * \brief Hang up an Asterisk channel belonging to a repeater
 * \param myrpt
 * \param chantype
 * \note Do not call if the channel does not exist (safe, but will emit a warning)
 */
void rpt_hangup(struct rpt *myrpt, enum rpt_chan_type chantype);

/*!
 * \brief Request a repeater channel
 * \param data rpt or rpt_link structure
 * \param chantype
 * \param flags
 * \note myrpt->lock must be held when calling
 * \retval 0 on success, -1 on failure
 */
int __rpt_request(void *data, struct ast_format_cap *cap, enum rpt_chan_type chantype, enum rpt_chan_flags flags);

#define rpt_request(data, cap, chantype) __rpt_request(data, cap, chantype, 0)

/*!
 * \brief Request a repeater channel not associated with a real device
 * \param myrpt
 * \param chantype
 * \param flags
 * \note myrpt->lock must be held when calling
 * \retval 0 on success, -1 on failure
 */
int __rpt_request_pseudo(void *data, struct ast_format_cap *cap, enum rpt_chan_type chantype, enum rpt_chan_flags flags);

#define rpt_request_pseudo(data, cap, chantype) __rpt_request_pseudo(data, cap, chantype, 0)

int dahdi_conf_create(struct ast_channel *chan, int *confno, int mode);

int dahdi_conf_add(struct ast_channel *chan, int confno, int mode);

int __rpt_conf_create(struct ast_channel *chan, struct rpt *myrpt, enum rpt_conf_type type, enum rpt_conf_flags flags, const char *file, int line);

int __rpt_conf_add(struct ast_channel *chan, struct rpt *myrpt, enum rpt_conf_type type, enum rpt_conf_flags flags, const char *file, int line);

#define rpt_conf_create(chan, myrpt, type, flags) __rpt_conf_create(chan, myrpt, type, flags, __FILE__, __LINE__)
#define rpt_conf_add(chan, myrpt, type, flags) __rpt_conf_add(chan, myrpt, type, flags, __FILE__, __LINE__)

#define rpt_conf_add_speaker(chan, myrpt) rpt_conf_add(chan, myrpt, RPT_CONF, RPT_CONF_CONF | RPT_CONF_LISTENER | RPT_CONF_TALKER)

#define rpt_tx_conf_add_speaker(chan, myrpt) rpt_conf_add(chan, myrpt, RPT_TXCONF, RPT_CONF_CONF | RPT_CONF_LISTENER | RPT_CONF_TALKER)

#define rpt_conf_add_announcer(chan, myrpt) rpt_conf_add(chan, myrpt, RPT_CONF, RPT_CONF_CONFANN)

#define rpt_conf_add_announcer_monitor(chan, myrpt) rpt_conf_add(chan, myrpt, RPT_CONF, RPT_CONF_CONFANNMON)

#define rpt_tx_conf_add_announcer(chan, myrpt) rpt_conf_add(chan, myrpt, RPT_TXCONF, RPT_CONF_CONFANN)

/*!
 * \brief Get the conference number of a DAHDI channel
 * \param chan DAHDI channel
 * \retval -1 on failure, conference number on success
 */
int dahdi_conf_fd_confno(struct ast_channel *chan);

/*!
 * \brief Play dialtone on a channel
 * \param chan
 * \retval 0 on success, -1 on failure
 */
int rpt_play_dialtone(struct ast_channel *chan);

/*!
 * \brief Play congestion tone on a channel
 * \param chan
 * \retval 0 on success, -1 on failure
 */
int rpt_play_congestion(struct ast_channel *chan);

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

/*!
 * \brief Wait for the DAHDI driver to physically write all audio to the hardware
 * \note Up to a max of 1 second
 * \note Only use with DAHDI channels!
 * \param chan
 * \retval 0 on success, -1 on failure
 */
int dahdi_write_wait(struct ast_channel *chan);
