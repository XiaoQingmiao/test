/* This code now might achieve messages sending and receviving from my_ID to dest_ID.
    We just need to change parameters in line 29 to control rp2040's communication.

   The code was modified from the demo code given by Hunter at https://github.com/vha3/Hunter-Adams-RP2040-Demos/tree/master/Networking/CAN
   It has:
    1. Multiple treads (protothread_send: sending messages)
                       (protothread_watchdog: preventing system hangs)
    2. Double cores (core 1 (core1_main()): sends messages, LED toggles for successful transmission)
                    (core 0 (main()): initializes sys_clk and LED, setup core1 for sending, setup receiving and watchdog)
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/clocks.h"
#include "hardware/watchdog.h"

#include "pt_cornell_rp2040_v1.h"
#include "can.h"


#define NEW_PAYLOAD_LEN  5              // the length of payload here

// ----------------------------------------------------------------------
// Initialize CAN driver
// ----------------------------------------------------------------------

CAN demo_can( 0x3234, 0x4234, 0x5555 ); // my_ID, dest_ID, broadcast_ID

// ----------------------------------------------------------------------
// Threads (Core 1 & 0)
// ----------------------------------------------------------------------

// Thread runs on core 1.
static PT_THREAD (protothread_send(struct pt *pt))
{
    PT_BEGIN(pt);

    // Brief delay before starting up
    sleep_ms(2000) ;

    // # of packets to send
    static int number_to_send = 1000000 ;

    while(1) {
        // If packets remain . . .
        if (number_to_send) {
            // Indicate that it is unsafe to transmit
            demo_can.set_unsafe_to_tx( 1 ) ;
            // Send a packet
            demo_can.sendPacket() ;
            // Decrement the remaining number of packets to send ;
            number_to_send -= 1 ;

            // Randomize the payload WHILE previous packet is being sent
            unsigned short new_payload[NEW_PAYLOAD_LEN];
            for (int i = 0; i < NEW_PAYLOAD_LEN; ++i) {
                new_payload[i] = ( unsigned short )( rand() & 0b0111111111111111 );
            }
            demo_can.set_payload( new_payload, NEW_PAYLOAD_LEN );
            
            // Print some data occasionally
            if (((number_to_send+1) % 1000)==0) {
                printf("Sent: %d\n", demo_can.get_number_sent()) ;
                printf("Received: %d\n", demo_can.get_number_received()) ;
                printf("Rejected: %d\n\n", demo_can.get_number_missed()) ;
            }
            // Wait until it's safe to send again
            while(demo_can.get_unsafe_to_tx()) {} ;
        }
        // If no packets remain, print some data
        else {
            sleep_ms(500) ;
            printf("Number sent: %d\n", demo_can.get_number_sent()) ;
            printf("Number received: %d\n", demo_can.get_number_received()) ;
            printf("Number rejected: %d\n\n", demo_can.get_number_missed()) ;
        }
    } 

    PT_END(pt);
}

// Thread runs on core 0
static PT_THREAD (protothread_watchdog(struct pt *pt))
{
    PT_BEGIN(pt);
    
    watchdog_enable(1000, 1);

    while(1) {
        sleep_ms(100) ;
        watchdog_update();
    } 

    PT_END(pt);
}

// ----------------------------------------------------------------------
// Main for Core 1 & 0 
// ----------------------------------------------------------------------

void tx_handler_wrapper() { demo_can.tx_handler(); }
void rx_handler_wrapper() { demo_can.rx_handler(); }

// Main for core 1
void core1_main() {
    // Setup the CAN transmitter on core 1
    demo_can.setupCANTX(tx_handler_wrapper) ;

    // Add the send thread to scheduler, and start it
    pt_add_thread(protothread_send) ;
    pt_schedule_start ;
}

// Main for core 0
int main() {
    // Overclock to 160MHz (divides evenly to 1 megabaud) (160/5/32=1)
    set_sys_clock_khz(OVERCLOCK_RATE, true) ;

    // Initialize stdio
    stdio_init_all();

    if (watchdog_caused_reboot()) {
        printf("Rebooted by Watchdog!\n");
    } else {
        printf("Clean boot\n");
    }

    // Print greeting
    printf("System starting up\n\n") ;

    // Initialize LED
    gpio_init(LED_PIN) ;
    gpio_set_dir(LED_PIN, GPIO_OUT) ;
    gpio_put(LED_PIN, 0) ;

    // start core 1 threads
    multicore_reset_core1();
    multicore_launch_core1(&core1_main);

    // Setup the CAN receiver on core 0
    demo_can.setupCANRX(rx_handler_wrapper) ;

    // Add thread to scheduler, and start it
    pt_add_thread(protothread_watchdog) ;
    pt_schedule_start ;
}
