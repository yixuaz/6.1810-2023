#define ICMP_ECHOREPLY   0 // Echo Reply
#define ICMP_ECHO 8 // Echo Request

struct icmp {
    uint8 type; // Type of message
    uint8 code; // Type sub code
    uint16 checksum; // Ones complement checksum of the struct
    union {
        struct {
            uint16 id;
            uint16 seq;
        } echo; // For Echo Request and Echo Reply messages
        uint32 unused;
    } un;
#define ih_id      un.echo.id
#define ih_seq     un.echo.seq
#define ih_unused  un.unused
};