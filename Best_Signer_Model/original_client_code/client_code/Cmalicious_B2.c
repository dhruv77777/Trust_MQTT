#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../include/mosquitto.h"
#include "../common/client_common.h"

int main() {
    struct client cli;
    cli.client_id = "C17";
    cli.issuer_broker = "B8";
    cli.publish_topic = "home/section17/device17";
    cli.subscribe_topic = "home/section17/service17";
    cli.message = "Hello from C17!";

    run_client(&cli);
    return 0;
}
