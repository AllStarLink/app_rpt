
//# Say a file - streams file to output channel
int sayfile(struct ast_channel *mychannel,char *fname);

int saycharstr(struct ast_channel *mychannel,char *str);

//# Say a number -- streams corresponding sound file
int saynum(struct ast_channel *mychannel, int num);

//# Say a phonetic words -- streams corresponding sound file
int sayphoneticstr(struct ast_channel *mychannel,char *str);

/* say a node and nodename. Try to look in dir referred to by nodenames in
config, and see if there's a custom node file to play, and if so, play it */
int saynode(struct rpt *myrpt, struct ast_channel *mychannel, char *name);

int play_tone_pair(struct ast_channel *chan, int f1, int f2, int duration, int amplitude);
int play_tone(struct ast_channel *chan, int freq, int duration, int amplitude);

//## Convert string into morse code
int send_morse(struct ast_channel *chan, char *string, int speed, int freq, int amplitude);

//# Send telemetry tones
int send_tone_telemetry(struct ast_channel *chan, char *tonestring);

int telem_any(struct rpt *myrpt,struct ast_channel *chan, char *entry);
