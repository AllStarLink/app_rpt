
int set_ft897(struct rpt *myrpt);
int set_ft100(struct rpt *myrpt);
int set_ft950(struct rpt *myrpt);
int set_ic706(struct rpt *myrpt);
int setkenwood(struct rpt *myrpt);
int set_tm271(struct rpt *myrpt);
int set_tmd700(struct rpt *myrpt);

/*! \brief Split ctcss frequency into hertz and decimal */
/*! \todo should be in rpt_utils for consistency? */
int split_ctcss_freq(char *hertz, char *decimal, char *freq);

int set_mode_ft897(struct rpt *myrpt, char newmode);
int set_mode_ft100(struct rpt *myrpt, char newmode);
int set_mode_ic706(struct rpt *myrpt, char newmode);

int simple_command_ft897(struct rpt *myrpt, char command);
int simple_command_ft100(struct rpt *myrpt, unsigned char command, unsigned char p1);

/*! \brief Dispatch to correct I/O handler  */
int setrem(struct rpt *myrpt);

int closerem(struct rpt *myrpt);

/*! \brief Dispatch to correct RX frequency checker */
int check_freq(struct rpt *myrpt, int m, int d, int *defmode);

/*! \brief Check TX frequency before transmitting */
/*! \retval 1 if tx frequency in ok. */
char check_tx_freq(struct rpt *myrpt);

/*! \brief Dispatch to correct frequency bumping function */
int multimode_bump_freq(struct rpt *myrpt, int interval);

/*! \brief Queue announcment that scan has been stopped */
void stop_scan(struct rpt *myrpt);

/*! \brief This is called periodically when in scan mode */
int service_scan(struct rpt *myrpt);

/*! \brief steer the radio selected channel to either one programmed into the radio
 * or if the radio is VFO agile, to an rpt.conf memory location. */
int channel_steer(struct rpt *myrpt, char *data);

int channel_revert(struct rpt *myrpt);
