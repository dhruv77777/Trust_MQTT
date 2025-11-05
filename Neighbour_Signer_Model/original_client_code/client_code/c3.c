#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../include/mosquitto.h"
#include "../common/client_common.h"

int main() {
    struct client cli;
    cli.client_id = "C3";
    cli.issuer_broker = "B1";
    cli.publish_topic = "firstfloor/bedroom";
    cli.subscribe_topic = "firstfloor/kitchen";
    cli.message = "Hello!";  // Set the message field

    run_client(&cli);
    return 0;
}