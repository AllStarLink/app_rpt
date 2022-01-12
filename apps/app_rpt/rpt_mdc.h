
void mdc1200_notify(struct rpt *myrpt,char *fromnode, char *data);

#ifdef	_MDC_DECODE_H_

void mdc1200_send(struct rpt *myrpt, char *data);

/*
	rssi_send() Send rx rssi out on all links.
*/
void rssi_send(struct rpt *myrpt);

void mdc1200_cmd(struct rpt *myrpt, char *data);

#ifdef	_MDC_ENCODE_H_

void mdc1200_ack_status(struct rpt *myrpt, short UnitID);

#endif
#endif

#ifdef	_MDC_ENCODE_H_

#define	MDCGEN_BUFSIZE 2000

struct mdcgen_pvt
{
	mdc_encoder_t *mdc;
	struct ast_format *origwfmt;
	struct ast_frame f;
	char buf[(MDCGEN_BUFSIZE * 2) + AST_FRIENDLY_OFFSET];
	unsigned char cbuf[MDCGEN_BUFSIZE];
} ;

struct mdcparams
{
	char	type[10];
	short	UnitID;
	short	DestID;
	short	subcode;
} ;

int mdc1200gen(struct ast_channel *chan, char *type, short UnitID, short destID, short subcode);
int mdc1200gen_start(struct ast_channel *chan, char *type, short UnitID, short destID, short subcode);

#endif
