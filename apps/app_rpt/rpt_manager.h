
/*!\brief callback to display list of locally configured nodes
   \addtogroup Group_AMI
 */
int manager_rpt_local_nodes(struct mansession *s, const struct message *m);

/*
 * Implement the RptStatus Manager Interface
 */
int manager_rpt_status(struct mansession *s, const struct message *m);
