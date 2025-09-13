#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../include/mosquitto.h"
#include "../common/client_common.h"

int main() {
    struct client cli;
    cli.client_id = "C7";
    cli.issuer_broker = "B3";
    cli.publish_topic = "thirdfloor/bedroom";
    cli.subscribe_topic = "thirdfloor/kitchen";
    cli.message = "Hello!";

    run_client(&cli);
    return 0;
}
