
/* Define function protos for function table here */

/*! \brief Internet linking function */
int function_ilink(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Autopatch up */
int function_autopatchup(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Autopatch down */
int function_autopatchdn(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Status */
int function_status(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief COP - Control operator */
int function_cop(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Remote base function */
int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Macro-oni (without Salami) */
int function_macro(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Playback a recording globally */
int function_playback(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Playback a recording locally */
int function_localplay(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Playback a meter reading */
int function_meter(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Set or reset a USER Output bit */
int function_userout(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Execute shell command */
int function_cmd(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
