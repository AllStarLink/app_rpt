
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
