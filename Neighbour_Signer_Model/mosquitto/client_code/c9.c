#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../include/mosquitto.h"
#include "../common/client_common.h"

int main() {
    struct client cli;
    cli.client_id = "C9";
    cli.issuer_broker = "B4";
    cli.publish_topic = "fourthfloor/bedroom";
    cli.subscribe_topic = "fourthfloor/kitchen";
    cli.message = "Hello!";

    run_client(&cli);
    return 0;
}
