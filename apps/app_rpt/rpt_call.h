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

/*! \file
 *
 * \brief app_rpt call helper functions
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 */

#ifdef RPT_EXPOSE_DAHDI
#define join_dahdiconf(chan, ci) __join_dahdiconf(chan, ci, __FILE__, __LINE__, __PRETTY_FUNCTION__)

int __join_dahdiconf(struct ast_channel *chan, struct dahdi_confinfo *ci, const char *file, int line, const char *function);
#endif

/*! \brief Disable CDR for a call */
int rpt_disable_cdr(struct ast_channel *chan);

int rpt_setup_call(struct ast_channel *chan, const char *addr, int timeout, const char *driver, const char *data, const char *desc, const char *callerid);

int rpt_make_call(struct ast_channel *chan, const char *addr, int timeout, const char *driver, const char *data, const char *desc, const char *callerid);

/*! \brief Routine to forward a "call" from one channel to another */
void rpt_forward(struct ast_channel *chan, char *dialstr, char *nodefrom);
