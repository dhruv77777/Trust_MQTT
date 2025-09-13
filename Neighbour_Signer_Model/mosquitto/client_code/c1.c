#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../include/mosquitto.h"
#include "../common/client_common.h"

int main() {
    struct client cli;
    cli.client_id = "C1";
    cli.issuer_broker = "B0";
    cli.publish_topic = "groundfloor/bedroom";
    cli.subscribe_topic = "groundfloor/kitchen";
    cli.message = "Hello!";

    run_client(&cli);
    return 0;
}
