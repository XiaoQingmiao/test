#include "hardware/pio.h"
#include "hardware/dma.h"
#include "can.pio.h"
#include "can.h"
#include "pico/stdlib.h"
#include "stdio.h"
#include <string.h>


// ----------------------------------------------------------------------
// Define buffers for storing studded/unstuffed packets for TX/RX
// ----------------------------------------------------------------------

// Assembled packet for transmission (unstuffed then stuffed)
unsigned short tx_packet_unstuffed[MAX_PACKET_LEN>>1] = {0} ;
unsigned short tx_packet_stuffed[MAX_STUFFED_PACKET_LEN>>1] = {0} ;
unsigned short * tx_packet_stuffed_pointer = &tx_packet_stuffed[0] ;

// Buffer for received packets (stuffed then unstuffed)
unsigned char rx_packet_stuffed[MAX_STUFFED_PACKET_LEN] = {0} ;
unsigned char rx_packet_unstuffed[MAX_PACKET_LEN] = {0} ;
unsigned char * rx_packet_stuffed_pointer = &rx_packet_stuffed[0] ;

// For re-initializing these buffers
unsigned char zero_packet[MAX_STUFFED_PACKET_LEN] = {0} ;

// ----------------------------------------------------------------------
// Define infrastructure globals
// ----------------------------------------------------------------------

// PIO blocks
PIO pio_0 = pio0 ;
PIO pio_1 = pio1 ;
// State machines
int can_tx_sm         = 0 ;
int can_idle_check_sm = 1 ;
int can_rx_sm         = 0 ;
// Select dma channels
int dma_chan_0  = 0 ;
int dma_chan_1  = 1 ;
int dma_chan_2  = 2 ;
int dma_chan_3  = 3 ;
int dma_chan_4  = 4 ;
// Dummy DMA source/destination for chained channel
unsigned int dummy_source = 0 ;
unsigned int dummy_dest   = 0 ;

// ----------------------------------------------------------------------
// Initialize CAN driver
// ----------------------------------------------------------------------

CAN::CAN(unsigned short my_arbitration, unsigned short arbitration, unsigned short network_broadcast )
    : my_arbitration( my_arbitration ),
      arbitration( arbitration ),
      network_broadcast( network_broadcast ),
      tx_idle_time( 500 ),
      reserve_byte( 0x55 ),
      payload_len( 10 ),
      number_sent( 0 ),
      number_received( 0 ),
      number_missed( 0 ),
      unsafe_to_tx( 1 )
{
}

// ----------------------------------------------------------------------
// Public accessor functions
// ----------------------------------------------------------------------

// ISR entered at the end of packet transmit
void CAN::tx_handler() {
    // Abort/reset DMA channel, clear FIFO, clear PIO irq
    resetTransmitter() ;
    // Toggle the LED
    gpio_put(LED_PIN, !gpio_get(LED_PIN)) ;
    number_sent += 1 ;
    // Signal to thread that it is safe to transmit
    unsafe_to_tx = 0 ;
}

// ISR entered when a packet is available for attempted receipt
void CAN::rx_handler() {
    // Abort/reset DMA channel
    resetReceiver() ;
    // Attempt packet receipt
    if (attemptPacketReceive()) {
        number_received += 1 ;
    } else {
        number_missed += 1 ;
    }
    // Clear the interrupt to receive the next message
    acceptNewPacket() ;
}

// Computes the checksum over a series of bytes
unsigned short CAN::culCalcCRC(char crcData, unsigned short crcReg) {
    for (int i = 0; i < 8; i++) {
        if (((crcReg & 0x8000) >> 8) ^ (crcData & 0x80)) {
            crcReg = (crcReg << 1) ^ CRC16_POLY;
        } else {
          crcReg = (crcReg << 1);
        }
        crcData <<= 1;
    }
    return crcReg;
}

// Packet transmission
unsigned short CAN::getBitShort(unsigned short * shorty, unsigned char bitnum) {
    switch(bitnum) {
        case 0:
            return ((*shorty & 0x8000) >> 15) ;
        case 1:
            return ((*shorty & 0x4000) >> 14) ;
        case 2:
            return ((*shorty & 0x2000) >> 13) ;
        case 3:
            return ((*shorty & 0x1000) >> 12) ;
        case 4:
            return ((*shorty & 0x0800) >> 11) ;
        case 5:
            return ((*shorty & 0x0400) >> 10) ;
        case 6:
            return ((*shorty & 0x0200) >> 9) ;
        case 7:
            return ((*shorty & 0x0100) >> 8) ;
        case 8:
            return ((*shorty & 0x0080) >> 7) ;
        case 9:
            return ((*shorty & 0x0040) >> 6) ;
        case 10:
            return ((*shorty & 0x0020) >> 5) ;
        case 11:
            return ((*shorty & 0x0010) >> 4) ;
        case 12:
            return ((*shorty & 0x0008) >> 3) ;
        case 13:
            return ((*shorty & 0x0004) >> 2) ;
        case 14:
            return ((*shorty & 0x0002) >> 1) ;
        case 15:
            return ((*shorty & 0x0001) >> 0) ;
        default:
            return 0xFFFF ;
    }
}

void CAN::modifyBitShort(unsigned short * shorty, unsigned char bitnum, unsigned short value) {
    switch(bitnum) {
        case 15:
            *shorty |= ((value << 0) & 0x0001) ;
            break ;
        case 14:
            *shorty |= ((value << 1) & 0x0002) ;
            break ;
        case 13:
            *shorty |= ((value << 2) & 0x0004) ;
            break ;
        case 12:
            *shorty |= ((value << 3) & 0x0008) ;
            break ;
        case 11:
            *shorty |= ((value << 4) & 0x0010) ;
            break ;
        case 10:
            *shorty |= ((value << 5) & 0x0020) ;
            break ;
        case 9:
            *shorty |= ((value << 6) & 0x0040) ;
            break ;
        case 8:
            *shorty |= ((value << 7) & 0x0080) ;
            break ;
        case 7:
            *shorty |= ((value << 8) & 0x0100) ;
            break ;
        case 6:
            *shorty |= ((value << 9) & 0x0200) ;
            break ;
        case 5:
            *shorty |= ((value << 10) & 0x0400) ;
            break ;
        case 4:
            *shorty |= ((value << 11) & 0x0800) ;
            break ;
        case 3:
            *shorty |= ((value << 12) & 0x1000) ;
            break ;
        case 2:
            *shorty |= ((value << 13) & 0x2000) ;
            break ;
        case 1:
            *shorty |= ((value << 14) & 0x4000) ;
            break ;
        case 0:
            *shorty |= ((value << 15) & 0x8000) ;
            break ;
        default:
            printf("Invalid argument to modifyBitChar   \n") ;
            break ;
    }
}

void CAN::bitStuff(unsigned short * unstuffed, unsigned short * stuffed) {
    // Clear the buffer
    memcpy(&stuffed[0], &zero_packet[0], MAX_STUFFED_PACKET_LEN) ;

    // Variables for monitoring position in each buffer
    int stuffed_index   = 0 ;
    int unstuffed_index = 0 ;
    int stuffed_bit     = 0 ;
    int unstuffed_bit   = 0 ;

    // Accumulated bit run length
    int bit_run_len = 1 ;

    // Memory of old bit value
    unsigned short new_val = 0 ;
    unsigned short old_val = 2 ;

    // Until we find the end of frame
    while ((*(unstuffed + unstuffed_index) != 0xFFFF) && (unstuffed_index < MAX_PACKET_LEN)) {
        new_val = getBitShort((unstuffed + unstuffed_index), unstuffed_bit) ;
        bit_run_len = (new_val==old_val)?(bit_run_len+1):1 ;
        old_val = new_val ;

        if (bit_run_len < 5) {
            modifyBitShort(stuffed+stuffed_index, stuffed_bit, new_val) ;
            stuffed_bit = (stuffed_bit<15)?(stuffed_bit+1):0 ;
            stuffed_index = (stuffed_bit==0)?(stuffed_index+1):stuffed_index ;
            unstuffed_bit = (unstuffed_bit<15)?(unstuffed_bit+1):0 ;
            unstuffed_index = (unstuffed_bit==0)?(unstuffed_index+1):unstuffed_index ;

        } else {
            modifyBitShort(stuffed+stuffed_index, stuffed_bit, new_val) ;
            stuffed_bit = (stuffed_bit<15)?(stuffed_bit+1):0 ;
            stuffed_index = (stuffed_bit==0)?(stuffed_index+1):stuffed_index ;

            modifyBitShort(stuffed+stuffed_index, stuffed_bit, !new_val) ;
            stuffed_bit = (stuffed_bit<15)?(stuffed_bit+1):0 ;
            stuffed_index = (stuffed_bit==0)?(stuffed_index+1):stuffed_index ;
            unstuffed_bit = (unstuffed_bit<15)?(unstuffed_bit+1):0 ;
            unstuffed_index = (unstuffed_bit==0)?(unstuffed_index+1):unstuffed_index ;

            bit_run_len = 1 ;
            old_val = !new_val ;
        }
    }

    // Pack out rest of that index with zeroes
    while(stuffed_bit <= 15) {
        modifyBitShort(stuffed+stuffed_index, stuffed_bit, 0) ;
        stuffed_bit += 1 ;
    }

    // Postpend a short of all ones
    stuffed_index += 1 ;
    *(stuffed + stuffed_index) = *(unstuffed + unstuffed_index) ;
}

// Computes and appends the checksum, then appends the EOF.
void CAN::sendPacket() {
    int i ;
    // Load arbitration
    tx_packet_unstuffed[0] = arbitration ;
    // Load reserve byte and payload length
    tx_packet_unstuffed[1] =  (((((unsigned short)reserve_byte)<<8) & 0xFF00) |
                             (((unsigned short)payload_len) & 0x00FF));
    // Load payload
    memcpy(&tx_packet_unstuffed[2], &payload[0], payload_len) ;
    // Compute checksum
    unsigned short checksum = CRC_INIT; // Init value for CRC calculation
    while (checksum == 0xFFFF) {
        tx_packet_unstuffed[1] ^= 0x8000 ;
        for (i = 0; i < ((payload_len>>1)+2); i++) {
          checksum = culCalcCRC((tx_packet_unstuffed[i]>>8)&0xFF, checksum);
          checksum = culCalcCRC((tx_packet_unstuffed[i])&0xFF, checksum);
        }
    }

    // Load checksum
    tx_packet_unstuffed[i] = checksum ;
    // Load EOF
    tx_packet_unstuffed[i+1] = 0xFFFF ;

    // Bit stuff the packet
    bitStuff(tx_packet_unstuffed, tx_packet_stuffed) ;

    // BEGIN TRANSMISSION
    dma_start_channel_mask((1u << dma_chan_0)) ;
}


// Packet reception
unsigned char CAN::getBitChar(unsigned char * byte, unsigned char bitnum) {
    switch(bitnum) {
        case 0:
            return ((*byte & 0x80) >> 7) ;
        case 1:
            return ((*byte & 0x40) >> 6) ;
        case 2:
            return ((*byte & 0x20) >> 5) ;
        case 3:
            return ((*byte & 0x10) >> 4) ;
        case 4:
            return ((*byte & 0x08) >> 3) ;
        case 5:
            return ((*byte & 0x04) >> 2) ;
        case 6:
            return ((*byte & 0x02) >> 1) ;
        case 7:
            return ((*byte & 0x01) >> 0) ;
        default:
            return 0xFF ;
    }
}

void CAN::modifyBitChar(unsigned char * byte, unsigned char bitnum, unsigned char value) {
    switch(bitnum) {
        case 7:
            *byte |= ((value << 0) & 0x01) ;
            break ;
        case 6:
            *byte |= ((value << 1) & 0x02) ;
            break ;
        case 5:
            *byte |= ((value << 2) & 0x04) ;
            break ;
        case 4:
            *byte |= ((value << 3) & 0x08) ;
            break ;
        case 3:
            *byte |= ((value << 4) & 0x10) ;
            break ;
        case 2:
            *byte |= ((value << 5) & 0x20) ;
            break ;
        case 1:
            *byte |= ((value << 6) & 0x40) ;
            break ;
        case 0:
            *byte |= ((value << 7) & 0x80) ;
            break ;
        default:
            printf("Invalid argument to modifyBitChar   \n") ;
            break ;
    }
}

// Unstuffs the first array and stores the result in the second.
void CAN::unBitStuff(unsigned char * stuffed, unsigned char * unstuffed) {
    // Clear the buffer
    memcpy(&unstuffed[0], &zero_packet[0], MAX_STUFFED_PACKET_LEN) ;

    // Variables for monitoring position in each buffer
    int stuffed_index   = 0 ;
    int unstuffed_index = 0 ;
    int stuffed_bit     = 0 ;
    int unstuffed_bit   = 0 ;

    // Accumulated bit run length
    int bit_run_len     = 0 ;

    // Memory of old bit value
    unsigned char new_val = 0 ;
    unsigned char old_val = 2 ;

    // Until we find the end of frame . . .
    while ((*(stuffed + stuffed_index) != 0xFF) && (stuffed_index < (MAX_STUFFED_PACKET_LEN))) {
        // Get a new bit, update the bit run length, and update the bit memory
        new_val = getBitChar((stuffed+stuffed_index), stuffed_bit) ;
        bit_run_len = (new_val==old_val)?(bit_run_len+1):1 ;
        old_val = new_val ;

        // If our bit run length is less than 5, update the unstuffed buffer
        // and increment position in each buffer.
        if (bit_run_len < 5) {

            modifyBitChar(unstuffed+unstuffed_index, unstuffed_bit, new_val) ;

            unstuffed_bit = (unstuffed_bit<7)?(unstuffed_bit+1):0 ;
            unstuffed_index = (unstuffed_bit==0)?(unstuffed_index+1):unstuffed_index ;

            stuffed_bit = (stuffed_bit<7)?(stuffed_bit+1):0 ;
            stuffed_index = (stuffed_bit==0)?(stuffed_index+1):stuffed_index ;

        } else {
            modifyBitChar(unstuffed+unstuffed_index, unstuffed_bit, new_val) ;
            unstuffed_bit = (unstuffed_bit<7)?(unstuffed_bit+1):0 ;
            unstuffed_index = (unstuffed_bit==0)?(unstuffed_index+1):unstuffed_index ;

            stuffed_bit = (stuffed_bit<7)?(stuffed_bit+1):0 ;
            stuffed_index = (stuffed_bit==0)?(stuffed_index+1):stuffed_index ;
            stuffed_bit = (stuffed_bit<7)?(stuffed_bit+1):0 ;
            stuffed_index = (stuffed_bit==0)?(stuffed_index+1):stuffed_index ;

            // Reset bit run length
            bit_run_len = 1 ;
            // We jumped over a stuffed bit, opposite polarity to
            // the last bit that we measured
            old_val = !new_val ;
        }
    }

}

// Check packet is valid (remain in rx_packet_unstuffed) or invalid.
unsigned char CAN::attemptPacketReceive() {
    int i ;

    // Unstuff the received packet
    unBitStuff(rx_packet_stuffed, rx_packet_unstuffed) ;

    // Check arbitration bits
    if ((rx_packet_unstuffed[0]!=((my_arbitration>>8)&0xFF))&&
        (rx_packet_unstuffed[0]!=((network_broadcast>>8)&0xFF))) {
        return 0 ;
    }
    if ((rx_packet_unstuffed[1]!=((my_arbitration)&0xFF))&&
        (rx_packet_unstuffed[1]!=((network_broadcast)&0xFF))) {
        return 0 ;
    }

    // Check packet length
    if (rx_packet_unstuffed[3] > MAX_PAYLOAD_SIZE) {
        // printf("Invalid packet length\n") ;
        return 0 ;
    }

    // Compute and check checksum
    unsigned short checksum = CRC_INIT; // Init value for CRC calculation
    for (i = 0; i < (rx_packet_unstuffed[3]+4); i++) {
      checksum = culCalcCRC((rx_packet_unstuffed[i])&0xFF, checksum);
    }
    if ((rx_packet_unstuffed[i]==((checksum>>8)&0xFF)) &&
        (rx_packet_unstuffed[i+1]==((checksum)&0xFF))) {
        return 1 ;
    } else {
        return 0 ;
    }
}

// Deive ISR
// resets the DMA channel when overrun on the RX DMA channel (a new node joins the network) 
void CAN::dma_handler() {
    // Clear the interrupt request
    dma_hw->ints0 = 1u << dma_chan_1;
    // Reset the DMA channel write address, and start the channel
    dma_channel_set_write_addr(dma_chan_1, rx_packet_stuffed_pointer, true) ;
}


// Setup CAN
void CAN::setupIdleCheck() {
    // Load PIO program onto PIO0
    uint can_idle_offset = pio_add_program(pio_0, &idle_check_program) ;

    // Initialize the PIO program
    idle_check_program_init(pio_0, can_idle_check_sm, can_idle_offset, CAN_TX+1, CLKDIV) ;

    // Zero the irq 1
    pio_interrupt_clear(pio_0, 1) ;

    // Start the PIO program
    pio_sm_set_enabled(pio_0, can_idle_check_sm, true) ;

    // Channel Two (sends data to TX PIO machine)
    dma_channel_config c2 = dma_channel_get_default_config(dma_chan_2);
    channel_config_set_transfer_data_size(&c2, DMA_SIZE_32);
    channel_config_set_read_increment(&c2, false);
    channel_config_set_write_increment(&c2, false);
    channel_config_set_dreq(&c2, DREQ_PIO0_TX1) ;
    channel_config_set_chain_to(&c2, dma_chan_3);

    dma_channel_configure(
        dma_chan_2,                     // Channel to be configured
        &c2,                            // The configuration we just created
        &pio_0->txf[can_idle_check_sm], // write address (idle check PIO TX FIFO)
        &tx_idle_time,                  // read address (variable containing idle time)
        1,                              // Number of transfers (aborts early!)
        false                           // Don't start immediately.
    );

    // Channel Three (resets channel 2)
    dma_channel_config c3 = dma_channel_get_default_config(dma_chan_3);
    channel_config_set_transfer_data_size(&c3, DMA_SIZE_32);
    channel_config_set_read_increment(&c3, false);
    channel_config_set_write_increment(&c3, false);
    channel_config_set_chain_to(&c3, dma_chan_2);

    dma_channel_configure(
        dma_chan_3,                     // Channel to be configured
        &c3,                            // The configuration we just created
        &dummy_dest,                    // write address (dummy)
        &dummy_source,                  // read address (dummy)
        1,                              // Number of transfers
        true                            // Start immediately.
    );
}

// Set up CAN TX machine
void CAN::setupCANTX(irq_handler_t handler) {
    // Power off transciever (avoids transients on bus)
    gpio_init(TRANSCIEVER_EN) ;
    gpio_set_dir(TRANSCIEVER_EN, GPIO_OUT) ;
    gpio_put(TRANSCIEVER_EN, 0) ;

    // Setup the idle checking system
    setupIdleCheck() ;

    // Load PIO programs onto PIO0
    uint can_tx_offset = pio_add_program(pio_0, &can_tx_program) ;

    // Initialize the PIO program
    can_tx_program_init(pio_0, can_tx_sm, can_tx_offset, CAN_TX, CLKDIV) ;

    // Setup interrupts for TX machine
    pio_interrupt_clear(pio_0, 0) ;
    pio_set_irq0_source_enabled(pio_0, pis_interrupt0, true) ;
    irq_set_exclusive_handler(PIO0_IRQ_0, handler) ;
    irq_set_enabled(PIO0_IRQ_0, true) ;

    // Channel Zero (sends data to TX PIO machine)
    dma_channel_config c0 = dma_channel_get_default_config(dma_chan_0);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_16);
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    channel_config_set_dreq(&c0, DREQ_PIO0_TX0) ;

    dma_channel_configure(
        dma_chan_0,                     // Channel to be configured
        &c0,                            // The configuration we just created
        &pio_0->txf[can_tx_sm],         // write address (transmit PIO TX FIFO)
        tx_packet_stuffed_pointer,      // read address (start of stuffed packet)
        sizeof(tx_packet_stuffed)>>1,   // Number of transfers (aborts early!)
        false                           // Don't start immediately.
    );

    // Start the TX PIO program (sets output high, among other things)
    pio_sm_set_enabled(pio_0, can_tx_sm, true) ;

    // Brief delay to allow GPIO to stabilize
    sleep_ms(1) ;

    // Power on transciever
    gpio_put(TRANSCIEVER_EN, 1) ;
}

// Set up CAN RX machine
void CAN::setupCANRX(irq_handler_t handler) {
    // Load pio program onto PIO 1
    uint can_rx_offset = pio_add_program(pio_1, &can_rx_program) ;

    // Initialize the PIO programs
    can_rx_program_init(pio_1, can_rx_sm, can_rx_offset, CAN_TX+1, CLKDIV) ;

    // Setup interrupts for RX machine
    pio_interrupt_clear(pio_1, 0) ;
    pio_set_irq0_source_enabled(pio_1, pis_interrupt0, true) ;
    irq_set_exclusive_handler(PIO1_IRQ_0, handler) ;
    irq_set_enabled(PIO1_IRQ_0, true) ;

    // Channel One (gets data from RX PIO machine)
    dma_channel_config c1 = dma_channel_get_default_config(dma_chan_1);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_8);
    channel_config_set_read_increment(&c1, false);
    channel_config_set_write_increment(&c1, true);
    channel_config_set_dreq(&c1, DREQ_PIO1_RX0) ;

    dma_channel_configure(
        dma_chan_1,                 // Channel to be configured
        &c1,                        // The configuration we just created
        rx_packet_stuffed_pointer,  // write address (receive buffer)
        &pio_1->rxf[can_rx_sm],     // read address (receive PIO RX FIFO)
        sizeof(rx_packet_stuffed),  // Number of transfers (aborts early!!)
        false                       // Don't start immediately.
    );
  
    // Tell DMA to rasie IRQ line 0 when channel 1 finished a block
    dma_channel_set_irq0_enabled(dma_chan_1, true);

    // Configure the processor to run dma_handler() when DMA IRQ 0 is asserted
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // Start the RX PIO machine
    pio_sm_set_enabled(pio_1, can_rx_sm, true) ;

    // Start the RX DMA channel
    dma_start_channel_mask((1u << dma_chan_1)) ;

}

// Call in the tx_handler ISR to reset the transmitter
inline void CAN::resetTransmitter() {
    // Abort the DMA channel sending data to the TX PIO (EOF found)
    dma_channel_abort(dma_chan_0) ;
    // Drain the TX FIFO
    pio_sm_drain_tx_fifo(pio_0, can_tx_sm) ;
    // Unstall the PIO state machine
    pio_interrupt_clear(pio_0, 0) ;
    // Reset the DMA channel read address, don't start channel yet
    dma_channel_set_read_addr(dma_chan_0, tx_packet_stuffed_pointer, false) ;
    // WHY IS THIS NECESSARY? Did not need this until I added the transcievers
    sleep_us(10) ;
}

// Call in the rx_handler ISR to reset the receiver
inline void CAN::resetReceiver() {
    // Full message received, abort DMA channel 2
    // disable the channel on IRQ0
    dma_channel_set_irq0_enabled(dma_chan_1, false);
    // abort the channel
    dma_channel_abort(dma_chan_1);
    // clear the spurious IRQ (if there was one)
    dma_channel_acknowledge_irq0(dma_chan_1);
    // re-enable the channel on IRQ0
    dma_channel_set_irq0_enabled(dma_chan_1, true);
    // Reset the DMA channel write address, and start the channel
    dma_channel_set_write_addr(dma_chan_1, rx_packet_stuffed_pointer, true) ;
}

// At end of receive ISR, clear interrupt to accept new packets
inline void CAN::acceptNewPacket() {
    pio_interrupt_clear(pio_1, 0) ;
}




