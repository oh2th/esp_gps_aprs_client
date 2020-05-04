/* String utils */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "strutils.h"

/* ------------------------------------------------------------------------ */
/* Decompose a URL into its components. */

void split_url(struct url_info *ui, char *url) {
    char *p = url;
    char *q;

    memset(ui, 0, sizeof *ui);

    q = strstr(p, "://");

    ui->scheme = p;
    *q = '\0';
    p = q+3;
    for (int i = 0; ui->scheme[i]; i++) {
        ui->scheme[i] = tolower(ui->scheme[i]);
    }

    q = strchr(p, '/');
    if (q) {
	*q = '/';
	ui->path = q;
	q = strchr(q, '#');
	if (q)
	    *q = '\0';
    } else {
	ui->path = "/";
    }

    ui->hostn = p;
    q = strchr(p, ':');
    if (q) {
	*q = '\0';
	ui->port = atoi(q+1);
    }
    if (ui->port == 0) {
        if (strcmp(ui->scheme,"http") == 0) {
            ui->port = 80;
        }
        if (strcmp(ui->scheme,"https") == 0) {
            ui->port = 443;
        }
    }
}
/* ------------------------------------------------------------------------ */
// convert a hex string such as "A489B1" into an array like [0xA4, 0x89, 0xB1].

uint8_t nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';

  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;

  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;

  return 0;  // Not a valid hexadecimal character
}

void hexToBytes(uint8_t *byteArray, const char *hexString) {
  uint8_t oddLength = strlen(hexString) & 1;

  uint8_t currentByte = 0;
  uint8_t byteIndex = 0;

  for (uint8_t charIndex = 0; charIndex < strlen(hexString); charIndex++) {
    uint8_t oddCharIndex = charIndex & 1;

    if (oddLength) {
        if (oddCharIndex) {
            currentByte = nibble(hexString[charIndex]) << 4;
        } else {
            currentByte |= nibble(hexString[charIndex]);
            byteArray[byteIndex++] = currentByte;
            currentByte = 0;
        }
    } else {
        if (!oddCharIndex) {
            currentByte = nibble(hexString[charIndex]) << 4;
        } else {
            currentByte |= nibble(hexString[charIndex]);
            byteArray[byteIndex++] = currentByte;
            currentByte = 0;
        }
    }
  }
}
/* ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------ */
