/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Naveen Albert <asterisk@phreaknet.org>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*!
 * \file
 *
 * \brief app_rpt call helper functions
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 */

/*! \brief Disable CDR for a call */
int rpt_disable_cdr(struct ast_channel *chan);

/*! \brief Prepare a channel for an outbound RPT call
 *
 * Sets channel read/write formats, disables CDRs where appropriate,
 * attaches the "Rpt" application and application data to the channel,
 * and sets the connected Caller ID and displayed extension for the
 * outgoing call so that the remote side sees the expected caller
 * information and node/extension.
 *
 * \param chan Channel initiating the outbound call
 * \param addr Destination address (driver-specific dial string or endpoint)
 * \param timeout Call timeout in seconds (used when actually placing the call)
 * \param driver Driver/protocol name (for logging; may be part of addr)
 * \param data Application data to set on the channel (rpt node/type)
 * \param desc Short description of the call purpose (for logging)
 * \param callerid Caller ID number/string to set on the outgoing call
 * \param node Node name or extension to set on the channel for visibility
 * \return 0 on success, non-zero on error
 */
int rpt_setup_call(struct ast_channel *chan, const char *addr, int timeout, const char *driver, const char *data,
	const char *desc, const char *callerid, const char *node);

/*! \brief Setup the channel and place an outbound RPT call
 *
 * This is a convenience wrapper that calls rpt_setup_call() to prepare
 * the channel and then invokes ast_call() to actually create the
 * outbound channel to the specified address.
 *
 * \param chan Channel initiating the outbound call
 * \param addr Destination address (driver-specific dial string or endpoint)
 * \param timeout Call timeout in seconds (passed to ast_call)
 * \param driver Driver/protocol name (for logging; may be part of addr)
 * \param data Application data to set on the channel (rpt node/type)
 * \param desc Short description of the call purpose (for logging)
 * \param callerid Caller ID number/string to set on the outgoing call
 * \param node Node name or extension to set on the channel for visibility
 * \return 0 on success, negative on error (returns result of rpt_setup_call() or ast_call())
 * \note Calls to this function call DNS lookup and is "blocking" up to DNS timeout time.
 */
int rpt_make_call(struct ast_channel *chan, const char *addr, int timeout, const char *driver, const char *data, const char *desc,
	const char *callerid, const char *node);

/*! \brief Routine to forward a "call" from one channel to another */
void rpt_forward(struct ast_channel *chan, char *dialstr, char *nodefrom);
