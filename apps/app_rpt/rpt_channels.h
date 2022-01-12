

/*
	send asterisk frame text message on the current tx channel
*/
int send_usb_txt(struct rpt *myrpt, char *txt);

void rpt_qwrite(struct rpt_link *l,struct ast_frame *f);
void rpt_safe_sleep(struct rpt *rpt,struct ast_channel *chan, int ms);
void rpt_forward(struct ast_channel *chan, char *dialstr, char *nodefrom);

/* must be called locked */
void cancel_pfxtone(struct rpt *myrpt);
