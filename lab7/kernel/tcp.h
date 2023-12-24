#ifndef TCP_H
#define TCP_H
// Size of the TCP control block table, used for storing information about active TCP connections
#define TCP_CB_TABLE_SIZE 16

// Define the range for source port numbers, typically used for dynamic allocation
#define TCP_SOURCE_PORT_MIN 49152
#define TCP_SOURCE_PORT_MAX 65535

// TCP_CB_STATE_*: Definitions of various states of the TCP control block, 
// representing different stages of a TCP connection
#define TCP_CB_STATE_CLOSED      0  // Connection is closed
#define TCP_CB_STATE_LISTEN      1  // Listening for connection requests from remote TCP endpoints
#define TCP_CB_STATE_SYN_SENT    2  // Synchronization request sent, waiting for acknowledgment
#define TCP_CB_STATE_SYN_RCVD    3  // Synchronization request received and acknowledged
#define TCP_CB_STATE_ESTABLISHED 4  // Connection established
#define TCP_CB_STATE_FIN_WAIT1   5  // Waiting for remote TCP's termination request or acknowledgment of the sent termination request
#define TCP_CB_STATE_FIN_WAIT2   6  // Termination request received from remote TCP, waiting for acknowledgment
#define TCP_CB_STATE_CLOSING     7  // Waiting for acknowledgment of termination request from remote TCP
#define TCP_CB_STATE_TIME_WAIT   8  // Waiting for enough time to pass to ensure remote TCP received acknowledgment of termination request
#define TCP_CB_STATE_CLOSE_WAIT  9  // Waiting for local user to issue a termination request
#define TCP_CB_STATE_LAST_ACK    10 // Waiting for acknowledgment of the initial termination request

// TCP_FLG_*: Definitions of TCP flag bits, used to control the state and behavior of TCP connections
#define TCP_FLG_FIN 0x01  // Finish control bit, used for releasing a connection
#define TCP_FLG_SYN 0x02  // Synchronize control bit, used for establishing connections
#define TCP_FLG_RST 0x04  // Reset control bit, used to forcibly break a connection
#define TCP_FLG_PSH 0x08  // Push control bit, indicates that the receiver should process these data immediately
#define TCP_FLG_ACK 0x10  // Acknowledgment control bit, indicates that the data from the counterpart has been received
#define TCP_FLG_URG 0x20  // Urgent control bit, used for processing urgent data

#define TCP_FLG_ISSET(x, y) ((x & 0x3f) & (y))

/*
 * Note on TCP Header Fields:
 * - sport and dport are the source and destination ports, respectively.
 * - seq is the sequence number of the first byte in the segment (except when SYN is present).
 * - ack is the acknowledgment number, indicating the next sequence number expected by the sender.
 * - off represents the size of the TCP header (in 32-bit words). 
 *   The upper 4 bits are the TCP header length, and the lower 4 bits are reserved.
 * - flags are control flags. Important ones include SYN (synchronize), ACK (acknowledgment), 
 *   FIN (finish), RST (reset), PSH (push), URG (urgent).
 * - win is the window size the sender is willing to receive.
 * - sum is the checksum, used for error-checking of the header and data.
 * - urp is the urgent pointer, which points to the end of the urgent data.
 */

// A TCP packet header (comes after an IP header).
struct tcp {
  uint16 sport; // Source port
  uint16 dport; // Destination port
  uint32 seq;   // Sequence number
  uint32 ack;   // Acknowledgment number
  uint8 off;    // Data offset (upper 4 bits) and Reserved (lower 4 bits)
  uint8 flags;  // Flags (URG, ACK, PSH, RST, SYN, FIN)
  uint16 win;   // Window size
  uint16 sum;   // Checksum
  uint16 urp;   // Urgent pointer
};

// since tcp is a stateful connection, we need this control block to maintain state
#define TCP_CB_WND_SIZE 2048
struct tcp_cb {
    uint8 state;           // State of the TCP connection (e.g., ESTABLISHED, FIN-WAIT, etc.)
    uint8 window[TCP_CB_WND_SIZE];    // Buffer for managing data flow control
                           // data awaiting acknowledgment when sending
                           // data received but not yet processed
    uint32 iss;            // Initial Send Sequence Number     
    uint32 irs;            // Initial Receive Sequence Number                               
    struct {
        uint32 nxt;        // Next sequence number to send
        uint16 wnd;        // Send window size
        uint32 una;        // Oldest unacknowledged sequence number
    } snd;
    struct {
        uint32 nxt;        // Next sequence number expected to receive
        uint16 wnd;        // Receive window size
    } rcv;
    struct tcp_cb *parent;
};

#endif
