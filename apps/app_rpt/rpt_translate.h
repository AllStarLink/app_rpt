
/*! \brief Translate function */
char func_xlat(struct rpt *myrpt, char c, struct rpt_xlat *xlat);

/*! \brief Translate APRStt DTMF to a callsign */
char aprstt_xlat(const char *instr, char *outstr);
