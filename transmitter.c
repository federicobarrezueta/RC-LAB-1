#include <stdio.h>
#include <string.h>
#include "application_layer.h"

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <port> <filename>\n", argv[0]);
        return 1;
    }

    ApplicationLayer al;
    strncpy(al.linkLayer.serialPort, argv[1], sizeof(al.linkLayer.serialPort) - 1);
    al.linkLayer.role             = TRANSMITTER;
    al.linkLayer.baudRate         = BAUDRATE;
    al.linkLayer.numTransmissions = 3;
    al.linkLayer.timeout          = 3;
    strncpy(al.filename, argv[2], sizeof(al.filename) - 1);
    al.maxPayloadSize             = 128;

    applicationLayer(al);
    return 0;
}
