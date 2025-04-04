
/*! \file
 *
 * \brief Generic serial I/O routines
 */

/*! \brief Generic serial port open command */
int serial_open(char *fname, int speed, int stop2);

/*
 * Return receiver ready status
 *
 * Return 1 if an Rx byte is available
 * Return 0 if none was available after a time out period
 * Return -1 if error
 */

int serial_rxready(int fd, int timeoutms);

/*
* Remove all RX characters in the receive buffer
*
* Return number of bytes flushed.
* or  return -1 if error
*
*/
int serial_rxflush(int fd, int timeoutms);

/*
 * Receive a string from the serial device
 */
int serial_rx(int fd, char *rxbuf, int rxmaxbytes, unsigned timeoutms, char termchr);

/*
 * Send a nul-terminated string to the serial device (without RX-flush)
 */
int serial_txstring(int fd, char *txstring);

/*
 * Write some bytes to the serial port, then optionally expect a fixed response
 */
int serial_io(int fd, const char *txbuf, char *rxbuf, int txbytes, int rxmaxbytes, unsigned int timeoutms, char termchr);

/*! \brief Set the Data Terminal Ready (DTR) pin on a serial interface */
int setdtr(struct rpt *myrpt, int fd, int enable);

/*! \brief open the serial port */
int openserial(struct rpt *myrpt, const char *fname);

int serial_remote_io(struct rpt *myrpt, unsigned char *txbuf, int txbytes, unsigned char *rxbuf, int rxmaxbytes, int asciiflag);

int setrbi(struct rpt *myrpt);
int setrtx(struct rpt *myrpt);
int setxpmr(struct rpt *myrpt, int dotx);
int setrbi_check(struct rpt *myrpt);
int setrtx_check(struct rpt *myrpt);

int civ_cmd(struct rpt *myrpt, unsigned char *cmd, int cmdlen);
