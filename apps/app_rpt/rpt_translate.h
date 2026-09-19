
/*!
 * \brief Translate function
 * \param myrpt pointer to repeater struct.
 * \param c character to process.
 * \param xlat translation state to advance.
 * \param[out] funcmatch set to 1 if c completed the funccharseq alternate sequence (the returned
 *                        character is a synthesized funcchars trigger, not a raw passthrough digit),
 *                        0 otherwise.
 */
char func_xlat(struct rpt *myrpt, char c, struct rpt_xlat *xlat, int *funcmatch);

/*! \brief Translate APRStt DTMF to a callsign */
char aprstt_xlat(const char *instr, char *outstr);
