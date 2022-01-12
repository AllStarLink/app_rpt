

/*
 * Generic serial port open command 
 */
int serial_open(char *fname, int speed, int stop2);

/*
 * Return receiver ready status
 *
 * Return 1 if an Rx byte is avalable
 * Return 0 if none was avaialable after a time out period
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
int serial_io(int fd, char *txbuf, char *rxbuf, int txbytes, int rxmaxbytes, unsigned int timeoutms, char termchr);


