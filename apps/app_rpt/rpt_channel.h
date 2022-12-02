
/*! \brief Multi-thread safe sleep routine */
void rpt_safe_sleep(struct rpt *rpt, struct ast_channel *chan, int ms);

/*! \brief Say a file - streams file to output channel */
int sayfile(struct ast_channel *mychannel, char *fname);

int saycharstr(struct ast_channel *mychannel, char *str);

/*! \brief Say a phonetic words -- streams corresponding sound file */
int sayphoneticstr(struct ast_channel *mychannel, char *str);

/*! \brief Say a number -- streams corresponding sound file */
int saynum(struct ast_channel *mychannel, int num);
