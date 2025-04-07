// =======================================================================
// can.h
// =======================================================================
// Declarations of the CAN bus functions

#ifndef CAN_H
#define CAN_H

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "can.pio.h"

// ----------------------------------------------------------------------
// Define Pins
// ----------------------------------------------------------------------

#define CAN_TX          6       // CAN RX is at CAN_TX+1
#define LED_PIN         25
#define TRANSCIEVER_EN  22

// ----------------------------------------------------------------------
// CAN parameters
// ----------------------------------------------------------------------

// Size of TX buffer
#define MAX_PAYLOAD_SIZE        16
#define MAX_PACKET_LEN          MAX_PAYLOAD_SIZE + 8
#define MAX_STUFFED_PACKET_LEN  MAX_PACKET_LEN + ( MAX_PACKET_LEN >> 1 )

// ----------------------------------------------------------------------
// Define clock and checksum parameters
// ----------------------------------------------------------------------

// Clock settings
#define OVERCLOCK_RATE 160000
#define CLKDIV         5
// Checksum polynomial and initial value
#define CRC16_POLY     0x8005
#define CRC_INIT       0xFFFF

// ----------------------------------------------------------------------
// CAN Bus
// ----------------------------------------------------------------------

class CAN {
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Public accessor functions
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  public:
    CAN( unsigned short my_arbitration, unsigned short arbitration, unsigned short network_broadcast );

    void set_my_arbitration( unsigned short my_arbitration ) {
      this->my_arbitration = my_arbitration;
    }
    void set_arbitration( unsigned short arbitration ) {
      this->arbitration = arbitration;
    }
    void set_network_broadcast( unsigned short network_broadcast ) {
      this->network_broadcast = network_broadcast;
    }
    void set_payload( unsigned short * new_payload, unsigned char len ) {
      payload_len = len;
      for (int i = 0; i < len; i++) {
          payload[i] = new_payload[i];
      }
    }
    unsigned short get_my_rbitration() { return my_arbitration; }
    unsigned short get_arbitration() { return arbitration; }
    unsigned short get_network_broadcast() { return network_broadcast; }
    unsigned short get_payload() { return payload[MAX_PAYLOAD_SIZE]; }


    // User interrupt service routine
    void tx_handler();
    void rx_handler();

    void set_number_sent( volatile int  number_sent ) {
      this->number_sent = number_sent;
    }
    void set_number_received( volatile int  number_received ) {
      this->number_received = number_received;
    }
    void set_number_missed( volatile int  number_missed ) {
      this->number_missed = number_missed;
    }
    void set_unsafe_to_tx( volatile int  unsafe_to_tx ) {
      this->unsafe_to_tx = unsafe_to_tx;
    }
    int get_number_sent() { return number_sent; }
    int get_number_received() { return number_received; }
    int get_number_missed() { return number_missed; }
    int get_unsafe_to_tx() { return unsafe_to_tx; }


    // Computes the checksum
    unsigned short culCalcCRC(char crcData, unsigned short crcReg);

    // Packet transmission
    unsigned short getBitShort(unsigned short * shorty, unsigned char bitnum);
    void modifyBitShort(unsigned short * shorty, unsigned char bitnum, unsigned short value);
    void bitStuff(unsigned short * unstuffed, unsigned short * stuffed);
    void sendPacket();

    // Packet reception
    unsigned char getBitChar(unsigned char * byte, unsigned char bitnum);
    void modifyBitChar(unsigned char * byte, unsigned char bitnum, unsigned char value);
    void unBitStuff(unsigned char * stuffed, unsigned char * unstuffed);
    unsigned char attemptPacketReceive();

    // Driver interrupt service routine (ISR)
    static void dma_handler(); // overrun on the RX DMA channel, then being reset

    // Setup CAN bus 
    void setupIdleCheck();
    void setupCANTX(irq_handler_t handler);
    void setupCANRX(irq_handler_t handler);

    // API helper functions
    static inline void resetTransmitter();
    static inline void resetReceiver();
    static inline void acceptNewPacket();

    protected:
      unsigned short my_arbitration, arbitration, network_broadcast;
      unsigned int tx_idle_time;    // time to wait (in bit times) for bus to be idle before TX
      unsigned char reserve_byte;   // reserve byte
      unsigned char payload_len;    // payload length in bytes (even)
      unsigned short payload[MAX_PAYLOAD_SIZE] = {0x1335, 0x5678, 0x9012, 0x3456, 0x7890};

      volatile int number_sent;     // # of sent messages
      volatile int number_received; // # of received messages
      volatile int number_missed;   // # of rejected packets
      volatile int unsafe_to_tx;    // flag for indicating that it is unsafe to transmit
};

#endif  // CAN_H