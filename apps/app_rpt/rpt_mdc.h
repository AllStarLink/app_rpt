
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
