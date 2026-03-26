#ifndef APPLICATION_LAYER_H
#define APPLICATION_LAYER_H

#include "link_layer.h"

typedef struct {
    LinkLayer linkLayer;
    char filename[256];   // file to send (Tx) or save as (Rx)
    int maxPayloadSize;   // max bytes per data packet
} ApplicationLayer;

void applicationLayer(ApplicationLayer params);

#endif
