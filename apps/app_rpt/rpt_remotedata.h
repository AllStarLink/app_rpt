
void send_newkey(struct ast_channel *chan);
void send_old_newkey(struct ast_channel *chan);

void handle_link_data(struct rpt *myrpt, struct rpt_link *mylink, char *str);

void handle_link_phone_dtmf(struct rpt *myrpt, struct rpt_link *mylink, char c);

void do_dtmf_local(struct rpt *myrpt, char c);

int handle_remote_dtmf_digit(struct rpt *myrpt,char c, char *keyed, int phonemode);

int handle_remote_data(struct rpt *myrpt, char *str);

int handle_remote_phone_dtmf(struct rpt *myrpt, char c, char *keyed, int phonemode);

/* 0 return=continue, 1 return = break, -1 return = error */
void local_dtmf_helper(struct rpt *myrpt,char c_in);
