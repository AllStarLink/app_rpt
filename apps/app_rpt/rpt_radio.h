
/*!
 * \brief Enable or disable CTCSS/DCS decode for RX
 * \param myrpt
 * \param enable 1 to enable, 0 to disable
 * \retval -1 on failure, 0 on success, 1 if not applicable to channel tech
 */
int rpt_radio_rx_set_ctcss(struct rpt *myrpt, int enable);
