
/*! \brief Multi-thread safe sleep routine */
void rpt_safe_sleep(struct rpt *rpt, struct ast_channel *chan, int ms);

/*! \brief Wait a configurable interval of time */
int wait_interval(struct rpt *myrpt, int type, struct ast_channel *chan);

/*! \brief Say a file - streams file to output channel */
int sayfile(struct ast_channel *mychannel, char *fname);

int saycharstr(struct ast_channel *mychannel, char *str);

/*! \brief Say a phonetic words -- streams corresponding sound file */
int sayphoneticstr(struct ast_channel *mychannel, char *str);

/*! \brief Say a number -- streams corresponding sound file */
int saynum(struct ast_channel *mychannel, int num);
