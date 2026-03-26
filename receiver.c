#include <stdio.h>
#include <string.h>
#include "application_layer.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <port> [output_filename]\n", argv[0]);
        return 1;
    }

    ApplicationLayer al;
    strncpy(al.linkLayer.serialPort, argv[1], sizeof(al.linkLayer.serialPort) - 1);
    al.linkLayer.role             = RECEIVER;
    al.linkLayer.baudRate         = BAUDRATE;
    al.linkLayer.numTransmissions = 3;
    al.linkLayer.timeout          = 3;
    al.filename[0]                = '\0';  // use filename from START packet
    al.maxPayloadSize             = 128;

    // optional: override output filename
    if (argc >= 3) strncpy(al.filename, argv[2], sizeof(al.filename) - 1);

    applicationLayer(al);
    return 0;
}
