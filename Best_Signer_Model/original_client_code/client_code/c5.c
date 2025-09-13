#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../include/mosquitto.h"
#include "../common/client_common.h"

int main() {
    struct client cli;
    cli.client_id = "C5";
    cli.issuer_broker = "B2";
    cli.publish_topic = "secondfloor/bedroom";
    cli.subscribe_topic = "secondfloor/kitchen";
    cli.message = "Hello!";

    run_client(&cli);
    return 0;
}
