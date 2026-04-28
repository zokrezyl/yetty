/* Telnet protocol constants (RFC 854, RFC 1073) */

#ifndef YETTY_TELNET_PROTOCOL_H
#define YETTY_TELNET_PROTOCOL_H

#include <stdint.h>

/* Telnet command bytes (RFC 854) */
#define TELNET_SE 240   /* End of subnegotiation */
#define TELNET_NOP 241  /* No operation */
#define TELNET_DM 242   /* Data mark */
#define TELNET_BRK 243  /* Break */
#define TELNET_IP 244   /* Interrupt process */
#define TELNET_AO 245   /* Abort output */
#define TELNET_AYT 246  /* Are you there */
#define TELNET_EC 247   /* Erase character */
#define TELNET_EL 248   /* Erase line */
#define TELNET_GA 249   /* Go ahead */
#define TELNET_SB 250   /* Subnegotiation begin */
#define TELNET_WILL 251 /* Will (agree to enable option) */
#define TELNET_WONT 252 /* Won't (refuse to enable option) */
#define TELNET_DO 253   /* Do (request to enable option) */
#define TELNET_DONT 254 /* Don't (request to disable option) */
#define TELNET_IAC 255  /* Interpret as command */

/* Telnet options */
#define TELOPT_BINARY 0 /* RFC 856 - Binary transmission */
#define TELOPT_ECHO 1   /* RFC 857 - Echo */
#define TELOPT_SGA 3    /* RFC 858 - Suppress Go Ahead */
#define TELOPT_TTYPE 24 /* RFC 1091 - Terminal type */
#define TELOPT_NAWS 31  /* RFC 1073 - Window size */

/* Terminal type subnegotiation */
#define TTYPE_IS 0
#define TTYPE_SEND 1

#endif /* YETTY_TELNET_PROTOCOL_H */
