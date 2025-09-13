#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../include/mosquitto.h"
#include "../common/client_common.h"

int main() {
    struct client cli;
    cli.client_id = "C12";
    cli.issuer_broker = "B6";
    cli.publish_topic = "sixthfloor/kitchen";
    cli.subscribe_topic = "sixthfloor/bedroom";
    cli.message = "Hello!";

    run_client(&cli);
    return 0;
}
