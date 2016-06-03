# Notes on development of Touchbridge protocol



## Touchbridge port mapping


### Standard ports

All Touchbridge nodes have the following ports available:

0  - Timestamp / Trigger
1  - Address Discovery
2  - Config
3  - Faults


### IO Port Classes

Each node may have none, one, or multiple, of the following port classes:

  - Port not present
  - Common port (see above)
  - Emergency Stop (set all outputs to safe state)
  - Alarms (e.g. output stage overheating, input undervoltage, etc.)
  - Reserved
  - Digital In (32 data bits, 32 mask bits)
  - Digital Out (32 data bits, 32 mask bits)
  - Analogue In (4x 16-bit channels)
  - Analogue, PWM or Servo Out (4x 16-bit channels)
  - Counter-Timer Channel
  - UART
  - SPI Channel
  - I2C Channel
  - Read Memory-mapped buffer (8-bit address space)
  - Write Memory-mapped buffer (8-bit address space)
  - Read Memory-mapped buffer (16-bit address space)
  - Write Memory-mapped buffer (16-bit address space)
  - Execute function on memory-mapped buffer
  - Stepper motor output
  - Kinematics buffer (accepts stream of motion commands)
  - General Purpose 32 bit input channel
  - General Purpose 32 bit output channel

If possible, ports should be able to function using single 8-byte CAN data
frames. Where more capacity is needed, more ports of the same class can be
used. For example, each analogue output port supports 4x 16-bit channels so
so to support 16 physical input channels, 4 ports would be needed.


## Address Discovery

It is somewhat inconvenient that the unique electronic ID provided on the STM32
parts is a 96-bit number (I would say this represents something of an extreme
level of optimism on behalf of the manufacturer as to the number of parts they
hope to produce) but the maximum CAN message size is 64 bits.  For the
avoidance of having to wade through the jungle of hash functions, the most
expedient solution would seem to be a two-message address discovery that goes
soemthing like this:


Master device sends all the following messages to port 1 at the broadcast
address.  Functionality of port 1 for none-broadcast messages is not
implemented and is reserved for future use.

The payload has 2 or 8 bytes.

Byte[0] bitfield (broadcast):
0: Return ID half-word if set
1: ID half-word to return (1=MSB, 0=LSB)
2: Match ID half-word if set
3: ID half-word to match (1=MSB, 0=LSB)
4: Assign address from Byte[1] if set (Byte[1] reserved if clear)
5: Set shortlist flag
6: Clear shortlist flag
7: Match if shortlist flag set (match happens before set/clear occurs)

Byte[1] bitfield:
0:5 Address to assign (Byte[0].4 is set) or Reserved (Byte[0].4 is clear)
6: Reserved
7: Override hard address if set (but doesn't change hard address)


Byte[2:7] = Half-word of 96-bit ID to match if Byte[0].2 set. Byte[0].3 states
            which half-word (Most Significant Word or Least Significant Word)
            These bytes need not be sent if Byte[0].2 is clear. If they are
            sent, they should be ignored by the recipient.


When devices power-up, their soft address byte is set to zero (unassigned.)

### Step 0

The master can optionally issue the following:
    Byte[0] = 0x10 (Reset all soft address assignments)
    Byte[1] = 0x00 (Reset all soft address assignments)
    Byte[2:7] = Don't care (or not sent)

This resets all soft address to 0 (unassigned) allowing the discovery process
to start afresh.

### Step 1

The master then sends the first message of the discovery process as follows:

    Byte[0] = 0x43 (Return MSW of ID & clear shortlist flag)
    Byte[1] = Reserved
    Byte[2:7] = Don't care (or not sent)

### Step 2

All devices without an assigned soft address then respond:

    Byte[0:5] = LSW of ID.
    Byte[6] = Previously assigned soft address (0 if unassigned)
    Byte[7] = Previously assigned hard address (0 if unassigned)

If the master receives no responses, then all addresses are assigned and the
discovery process stops.

### Step 3

Some collisions will then take place and the master will receive the message
which is most dominant. The master responds with the following:

    Byte[0] = 0x2D (Return LSW, match MSW, set shortlist flag)
    Byte[1] = 0 (Reserved)
    Byte[2:7] = MSW of previously received ID

### Step 4

All devices which match the MSW of the ID in bytes[2:7] set their shortlist
flag. They then respond as follows:

    Byte[0:5] = LSW of ID.
    Byte[6] = Previously assigned soft address (0 if unassigned)
    Byte[7] = Previously assigned hard address (0 if unassigned)

### Step 5

The master will then receive the most dominant of these and responds as follows:
    Byte[0] = 0xD4 (Assign Address if on shortlist, reset shortlist flag)
    Byte[1] = Addr
    Byte[2:7] = LSW of previously received ID

The device which has its shortlist flag set and matches the ID LSW then sets
its soft address to that provided in byte[1]. It will not respond to any
further assignment calls.

### Step 6

Go to Step 1 and repeat until the master receives no new responses to Step 1.

Note that due to automatic re-transmission by CAN controllers in the devices,
the master may in fact receive a whole list of responses to Step 1, subject to
available buffer sizes, etc. The master could then use these responses to speed
up the processes or it could ignore them.


## Configuration

Port 2 is the config port. Request format is:

### Configuration Request

Byte[0] is a command byte. Bitfield:

    Bits 0-6: Configuration Command
    Bit6: Port configuration if set, global configuration if clear
    Bit7: Configuration write if set, configuration read if clear

Global Configuration Commands:

  - NOP - Don't transmit anything in response
  - Ping (echo back any request data unchanged, perform no other action)
  - Get Touchbridge protocol number (two bytes, major & minor version num)
  - Node Reset. Reset node state to as close as possible to power-up state
  - Return least significant ID half-word
  - Return most significant ID half-word
  - Set hard address to value in Byte[1] and write to non-volatile memory
  - Blink the LEDs (alternate TBG & STAT leds) if Byte[1] non-zero
  - Get Product ID string (see 'strings' section below)
  - Get firmware version string (see 'strings' section below)
  - User ID string (see 'strings' section below)
  - Write the user ID string to non-volatile memory

For global configuration the message data format is:

    Byte[1:7]: Config Data

For port configuration the format is:

    Byte[1] Port Number
    Byte[2:7]: Config Data

    TODO: find some nicer way of specifying length of data field for config params

### Configuration Response

Normal response (data depends on command) or error response.

### A note about strings

For any of the commands which involve getting / setting strings, Byte[1]
represents a byte offset into the string. The string has a maximum size of 256
bytes, including a null terminator. When getting strings, the response message
will contain up to 8 bytes of the string, starting at the supplied offset. When
setting strings, the request message contains up to 6 bytes of the string in
Bytes[2:7] which will be written starting at the offset supplied in Byte[1].

### Purpose of the User ID String.

The User ID string can be any string up to 256 characters long (including a
terminating null \0 character.) It can be used to identify the specific role of
a board in the user's application.

### Port-specific configuration


#### Common to all ports

Keys:

    0: Get port class & maximum config key (read only)
       Response:
            Byte[0]: port class, zero if port not present.
            Byte[1]: max config key

    1: Get port text description (read only)
       Request Bytes[2]: offset into string
       Response: up to 8 bytes of string
        
    2: Get port config description (read only)
       Request Bytes[2]: config number
       Request Bytes[3]: offset into string
       Response: up to 8 bytes of string

    3: Enable triggered messages on this port. Causes message to be emmitted
       when timestamp or trigger sent to port 0. Only applicable to ports
       which can return data (i.e. usually input ports only)
       Request Bytes[2] - enable if non-zero.
       Reset value: disabled.

    4-7: Reserved

#### Digital Input Ports

Keys:

    8: Input Present mask. Can be used to tell application which inputs are
       in use. Mainly of use when node operating autonomously rather than in
       controller-peripheral setup.
       Request Bytes[2:5] are a 32-bit mask.
       Reset value: each physical input channels present has its bit set.
    9: Rising edge enable mask for asynchronous event indications.
       Request Bytes[2:5] are a 32-bit mask.
       Reset value: 0 (all disabled.)
   10: Falling edge enable mask for asynchronous event indications.
       Request Bytes[2:5] are a 32-bit mask.
       Reset value: 0 (all disabled.)
   11: Debounce enable mask for asynchronous event indications.
       Request Bytes[2:5] are a 32-bit mask.
       Reset valu: 0xffffffff (all enabled.)
   12: Debounce time (common to all inputs on a port.)
       Request Byte[2] - debounce time in milliseconds.
       Reset value: 20ms.


