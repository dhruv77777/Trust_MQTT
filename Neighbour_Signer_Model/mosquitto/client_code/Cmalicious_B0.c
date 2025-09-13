#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../include/mosquitto.h"
#include "../common/client_common.h"

int main() {
    struct client cli;
    cli.client_id = "C16";
    cli.issuer_broker = "B8";
    cli.publish_topic = "home/section16/device16";
    cli.subscribe_topic = "home/section16/service16";
    cli.message = "Hello from C16!";

    run_client(&cli);
    return 0;
}
