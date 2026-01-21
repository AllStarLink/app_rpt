
/*!
 * \brief Enable or disable CTCSS/DCS decode for RX
 * \param myrpt
 * \param enable 1 to enable, 0 to disable
 * \retval -1 on failure, 0 on success, 1 if not applicable to channel tech
 */
int rpt_radio_rx_set_ctcss_decode(struct rpt *myrpt, int enable);

/*!
 * \brief Block CTCSS/DCS encode
 * \param chan
 * \param block 1 to block, 0 to not block
 * \retval -1 on failure, 0 on success, 1 if not applicable to channel tech
 */
int dahdi_radio_set_ctcss_encode(struct ast_channel *chan, int block);

/*! \note Based on DAHDI_RADPAR values in dahdi/user.h */
enum rpt_radpar {
	RPT_RADPAR_IGNORECT = 3,
	RPT_RADPAR_NOENCODE  = 4,
	RPT_RADPAR_UIODATA = 14,
	RPT_RADPAR_UIOMODE = 15,
	RPT_RADPAR_REMMODE = 16,
	RPT_RADPAR_REMCOMMAND = 17,
};

enum rpt_radpar_data {
	RPT_RADPAR_REM_NONE = 0,
	RPT_RADPAR_REM_RBI1 = 1,
	RPT_RADPAR_REM_SERIAL = 2,
	RPT_RADPAR_REM_SERIAL_ASCII = 3,
};

int rpt_radio_set_param(struct ast_channel *chan, struct rpt *myrpt, enum rpt_radpar par, enum rpt_radpar_data data);

int rpt_radio_set_remcommand_data(struct ast_channel *chan, unsigned char *data, int len);

int rpt_pciradio_serial_remote_io(struct rpt *myrpt, unsigned char *txbuf, int txbytes, unsigned char *rxbuf, int rxmaxbytes, int asciiflag);
