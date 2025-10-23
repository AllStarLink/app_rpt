
/* Define function protos for function table here */

/*! \brief Internet linking function */
enum rpt_function_response function_ilink(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source command_source,
	struct rpt_link *mylink);

/*! \brief Autopatch up */
enum rpt_function_response function_autopatchup(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source,
	struct rpt_link *mylink);

/*! \brief Autopatch down */
enum rpt_function_response function_autopatchdn(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source,
	struct rpt_link *mylink);

/*! \brief Status */
enum rpt_function_response function_status(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source, struct rpt_link *mylink);

/*! \brief COP - Control operator */
enum rpt_function_response function_cop(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source, struct rpt_link *mylink);

/*! \brief Remote base function */
enum rpt_function_response function_remote(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source, struct rpt_link *mylink);

/*! \brief Macro-oni (without Salami) */
enum rpt_function_response function_macro(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source, struct rpt_link *mylink);

/*! \brief Playback a recording globally */
enum rpt_function_response function_playback(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source, struct rpt_link *mylink);

/*! \brief Playback a recording locally */
enum rpt_function_response function_localplay(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source, struct rpt_link *mylink);

/*! \brief Playback a meter reading */
enum rpt_function_response function_meter(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source, struct rpt_link *mylink);

/*! \brief Set or reset a USER Output bit */
enum rpt_function_response function_userout(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source, struct rpt_link *mylink);

/*! \brief Execute shell command */
enum rpt_function_response function_cmd(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source, struct rpt_link *mylink);

/*! \brief Find rpt_link by name for AO2 callback*/
int rpt_link_find_by_name(void *obj, void *arg, int flags);
