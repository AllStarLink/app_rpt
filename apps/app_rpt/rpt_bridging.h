
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

/*!
 * \brief Hang up an Asterisk channel belonging to a repeater
 * \param myrpt
 * \param chantype
 * \note Do not call if the channel does not exist (safe, but will emit a warning)
 */
void rpt_hangup(struct rpt *myrpt, enum rpt_chan_type chantype);

/*!
 * \brief Request a repeater channel
 * \param myrpt
 * \param chantype
 * \note myrpt->lock must be held when calling
 * \retval 0 on success, -1 on failure
 */
int rpt_request(struct rpt *myrpt, struct ast_format_cap *cap, enum rpt_chan_type chantype);

/*!
 * \brief Request a repeater channel not associated with a real device
 * \param myrpt
 * \param chantype
 * \note myrpt->lock must be held when calling
 * \retval 0 on success, -1 on failure
 */
int rpt_request_pseudo(struct rpt *myrpt, struct ast_format_cap *cap, enum rpt_chan_type chantype);
