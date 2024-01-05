
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
	RPT_CONF_NORMAL = 0,
	RPT_CONF_MONITOR = 1,
	RPT_CONF_MONITORTX = 2,
	RPT_CONF_CONF = 4,
	RPT_CONF_CONFANN = 5,
	RPT_CONF_CONFMON = 6,
	RPT_CONF_CONFANNMON = 7,
	RPT_CONF_LISTENER = 0x100,
	RPT_CONF_TALKER = 0x200,
};

enum rpt_chan_flags {
	RPT_LINK_CHAN = (1 << 0),
};

/*!
 * \brief Set the bridging technology to use
 * \param usedahdi 1 to use DAHDI pseudo channels and conferencing,
 *                 0 to use softmix Asterisk bridges
 * \note This setting MUST NOT be changed at runtime.
 *       It should only be set once when app_rpt loads.
 *       Completely unload the module and load it again to load new setting.
 * \retval 0 on success, -1 on failure
 */
int rpt_set_bridging_subsystem(int usedahdi);

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
 * \brief Request a pseudo channel
 * \param cap
 * \param name
 * \return channel on success
 * \return NULL on failure
 */
struct ast_channel *rpt_request_pseudo_chan(struct ast_format_cap *cap, const char *name);

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

int __rpt_conf_create(struct ast_channel *chan, struct rpt *myrpt, enum rpt_conf_type type, enum rpt_conf_flags flags, const char *file, int line);

int __rpt_conf_add(struct ast_channel *chan, struct rpt *myrpt, enum rpt_conf_type type, enum rpt_conf_flags flags, const char *file, int line);

/*!
 * \brief Clean up any bridge state for a repeater
 * \param myrpt
 */
void rpt_bridge_cleanup(struct rpt *myrpt);

int rpt_equate_tx_conf(struct rpt *myrpt);

#define rpt_conf_create(chan, myrpt, type, flags) __rpt_conf_create(chan, myrpt, type, flags, __FILE__, __LINE__)
#define rpt_conf_add(chan, myrpt, type, flags) __rpt_conf_add(chan, myrpt, type, flags, __FILE__, __LINE__)

#define rpt_conf_add_speaker(chan, myrpt) rpt_conf_add(chan, myrpt, RPT_CONF, RPT_CONF_CONF | RPT_CONF_LISTENER | RPT_CONF_TALKER)

#define rpt_tx_conf_add_speaker(chan, myrpt) rpt_conf_add(chan, myrpt, RPT_TXCONF, RPT_CONF_CONF | RPT_CONF_LISTENER | RPT_CONF_TALKER)

#define rpt_conf_add_announcer(chan, myrpt) rpt_conf_add(chan, myrpt, RPT_CONF, RPT_CONF_CONFANN)

#define rpt_conf_add_announcer_monitor(chan, myrpt) rpt_conf_add(chan, myrpt, RPT_CONF, RPT_CONF_CONFANNMON)

#define rpt_tx_conf_add_announcer(chan, myrpt) rpt_conf_add(chan, myrpt, RPT_TXCONF, RPT_CONF_CONFANN)

/*! \note Used in app_rpt.c */
int rpt_call_bridge_setup(struct rpt *myrpt, struct ast_channel *mychannel, struct ast_channel *genchannel);

/*! \note Used in app_rpt.c */
int rpt_mon_setup(struct rpt *myrpt);

/*! \note Used in app_rpt.c */
int rpt_parrot_add(struct rpt *myrpt);

/*!
 * \brief Get if channel is muted in conference
 * \param chan
 * \param myrpt
 * \retval 0 if not muted, 1 if muted
 */
int rpt_conf_get_muted(struct ast_channel *chan, struct rpt *myrpt);

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

/*!
 * \brief Flush events on a DAHDI channel
 * \note Only use with DAHDI channels!
 * \param chan
 * \retval 0 on success, -1 on failure
 */
int dahdi_flush(struct ast_channel *chan);

/*!
 * \brief Increase buffer space on DAHDI channel, if needed to accomodate samples
 * \note Only use with DAHDI channels!
 * \param chan
 * \param samples
 * \retval 0 on success, -1 on failure
 */
int dahdi_bump_buffers(struct ast_channel *chan, int samples);

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
