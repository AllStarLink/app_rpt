
#include "asterisk.h"

#include <dahdi/user.h>

#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_radio.h"

static int dahdi_set_radpar(struct ast_channel *chan, int param, int data)
{
	struct dahdi_radio_param r;

	memset(&r, 0, sizeof(struct dahdi_radio_param));

	r.radpar = param;
	r.data = data;

	if (ioctl(ast_channel_fd(chan, 0), DAHDI_RADIO_SETPARAM, &r) == -1) {
		/* Don't log as error here, in case it's nothing to worry about */
		ast_debug(1, "Failed to set radio parameter on %s: %s\n", ast_channel_name(chan), strerror(errno));
		return -1;
	}
	return 0;
}

static int dahdi_radio_set_ctcss_decode(struct ast_channel *chan, int enable)
{
	int res = dahdi_set_radpar(chan, DAHDI_RADPAR_IGNORECT, enable);
	if (res) {
		ast_log(LOG_WARNING, "Failed to set ignore CTCSS/DCS decode: %s\n", strerror(errno));
	}
	return res;
}

int rpt_radio_rx_set_ctcss_decode(struct rpt *myrpt, int enable)
{
	if (!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "DAHDI")) {
		return dahdi_radio_set_ctcss_decode(myrpt->dahdirxchannel, enable);
	}
	return 1;
}

int dahdi_radio_set_ctcss_encode(struct ast_channel *chan, int block)
{
	return dahdi_set_radpar(chan, DAHDI_RADPAR_NOENCODE, block);
}

int rpt_radio_set_param(struct ast_channel *chan, struct rpt *myrpt, enum rpt_radpar par, enum rpt_radpar_data data)
{
	/* rpt_radpar and rpt_radpar_data maps to RPT_RADPAR values exactly,
	 * so for now we can just pass them on. */
	return dahdi_set_radpar(chan, par, data);
}
