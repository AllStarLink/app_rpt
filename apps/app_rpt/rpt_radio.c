
#include "asterisk.h"

#include <dahdi/user.h>

#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_radio.h"

static int dahdi_radio_set_ctcss(struct ast_channel *chan, int enable)
{
	struct dahdi_radio_param r;

	memset(&r, 0, sizeof(struct dahdi_radio_param));
	r.radpar = DAHDI_RADPAR_IGNORECT;
	r.data = enable;
	if (ioctl(ast_channel_fd(chan, 0), DAHDI_RADIO_SETPARAM, &r)) {
		ast_log(LOG_WARNING, "Failed to set ignore CTCSS/DCS decode: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int rpt_radio_rx_set_ctcss(struct rpt *myrpt, int enable)
{
	if (!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "DAHDI")) {
		return dahdi_radio_set_ctcss(myrpt->dahdirxchannel, enable);
	}
	return 1;
}
