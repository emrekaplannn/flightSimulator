#include "pragmas.h"
#include <xc.h>
#include <stdio.h>
#include <string.h>

#define _XTAL_FREQ 40000000 // Define the system frequency for __delay_ms
#define BAUDRATE 115200
#define BRGVAL ((_XTAL_FREQ / BAUDRATE) / 16) - 1

// Enums for operation modes
typedef enum { IDLE, ACTIVE, END } Mode;
Mode current_mode = IDLE;

// Global variables for distance, speed, and mode
unsigned int distance = 0;
unsigned int speed = 0;
unsigned int altitude_period = 0;
char altitude_flag = 0;
unsigned int altitude_timer = 0; // Timer for altitude reporting
unsigned int finish = 1; // Break while loop to end program
unsigned int start = 0; // to handle first go command
unsigned int switch_flag=0; // to handle manual mode
unsigned int portb_pressed = 0;

// Transmit buffer
char tx_buffer[50];
unsigned int tx_index = 0;
unsigned int tx_length = 0;
char *message_g;

// Function prototypes
void initADC();
void initUSART();
void initInterrupts();
void transmitChar(char chr);
void sendResponse(const char* message);
void handleCommand(char* command);
void handleGOCommand(char* command);
void handleENDCommand();
void handleSPDCommand(char* command);
void handleALTCommand(char* command);
void handleMANCommand(char* command);
void handleLEDCommand(char* command);
void sendDistanceResponse();
void sendAltitudeResponse();
void sendButtonPressResponse();
unsigned int readADC();
unsigned int mapAltitude(unsigned int adcValue);


void sendResponse(const char* message) {
    message_g = message;
    TXSTAbits.TXEN = 1;
}

void debugPrint(const char* message) {
    sendResponse(message);
}

void handleCommand(char* command) {
    if (strncmp(command, "$GOO", 4) == 0) {
        handleGOCommand(command);
    } else if (strncmp(command, "$END", 4) == 0) {
        handleENDCommand();
    } else if (strncmp(command, "$SPD", 4) == 0) {
        handleSPDCommand(command);
    } else if (strncmp(command, "$ALT", 4) == 0) {
        handleALTCommand(command);
    } else if (strncmp(command, "$MAN", 4) == 0) {
        handleMANCommand(command);
    } else if (strncmp(command, "$LED", 4) == 0) {
        handleLEDCommand(command);
    }
}

void handleGOCommand(char* command) {
    if (!start) {
        current_mode = ACTIVE;
        TMR0H = 0xF0;        // Load high byte of timer value
        TMR0L = 0xCA;        // 100 ms interrupt rate
        INTCONbits.TMR0IE = 1; // Enable Timer0 interrupt
        start = 1;
    }
    unsigned int distance_value;
    sscanf(&command[4], "%04x", &distance_value); // Adjust the index to parse correctly
    distance = distance_value;
   
}

void handleENDCommand() {
    //debugPrint("Handling END Command\n");
    current_mode = END;
    INTCONbits.TMR0IE = 0;
    sendResponse("$END#");
    finish = 0; // To finish program
}

void handleSPDCommand(char* command) {
    //debugPrint("Handling SPD Command\n");
    sscanf(&command[4], "%04x", &speed);
}

void handleALTCommand(char* command) {
    //debugPrint("Handling ALT Command\n");
    sscanf(&command[4], "%04x", &altitude_period);
    if (altitude_period == 0) {
        altitude_flag = 0;
    } else {
        altitude_flag = 1;
    }
}

void handleMANCommand(char* command) {
    //debugPrint("Handling MAN Command\n");
    sscanf(&command[4], "%02x", &switch_flag);
    if (switch_flag == 1) {
        // Enable PORTB interrupt-on-change
        INTCONbits.RBIE = 1;
    } else {
        // Disable PORTB interrupt-on-change
        INTCONbits.RBIE = 0;
    }
}

void handleLEDCommand(char* command) {
    if (switch_flag) {
        //debugPrint("Handling LED Command\n");
        unsigned char led_num;
        sscanf(&command[4], "%02x", &led_num);
        // LATB = (LATB & 0xF0) | (1 << led_num); 
        // Update PORTB to light the corresponding LED
        if (led_num == 0) {
            LATD = 0x00; // Clear RD0
            LATC = 0x00; // Clear RC0
            LATB = 0x00; // Clear RB0
            LATA = 0x00; // Clear RA0
        } else {
            switch (led_num) {
                case 1:
                    LATD = (LATD & 0xFE) | 0x01; // Set RD0
                    break;
                case 2:
                    LATC = (LATC & 0xFE) | 0x01; // Set RC0
                    break;
                case 3:
                    LATB = (LATB & 0xFE) | 0x01; // Set RB0
                    break;
                case 4:
                    LATA = (LATA & 0xFE) | 0x01; // Set RA0
                    break;
                default:
                    break;
            }
        }
    }
}

void updateDistance(){
    // Update distance based on speed
    if (distance >= speed) {
        distance -= speed;
    } else {
        distance = 0;
        //current_mode = END;
        //sendResponse("$END#");
    }
}

void sendDistanceResponse() {
    sprintf(tx_buffer, "$DST%04X#", distance);
    sendResponse(tx_buffer);
}

void sendAltitudeResponse() {
    unsigned int adcValue = readADC();
    unsigned int altitude = mapAltitude(adcValue);
    sprintf(tx_buffer, "$ALT%04X#", altitude);
    sendResponse(tx_buffer);
}

void sendButtonPressResponse() {
    unsigned int button = portb_pressed;
    sprintf(tx_buffer, "$PRS%02X#", button);
    sendResponse(tx_buffer);
}

void initADC() {
    // Initialize the ADC module
    TRISHbits.TRISH4 = 1; // Set PORTH4 as input
    ADCON0 = 0x31; // Enable ADC, select AN12 channel
    ADCON1 = 0x0E; // Configure VDD and VSS as reference voltages, and select AN12 as analog input
    ADCON2 = 0xA9; // Right justify result, acquisition time, and conversion clock
    PIE1bits.ADIE = 1; // Enable ADC interrupt
    IPR1bits.ADIP = 1; // Set ADC interrupt to high priority
}

void initUSART() {
    // Configure USART transmitter/receiver
    TXSTA1bits.BRGH = 1;  // High-speed mode
    TXSTA1bits.SYNC = 0;  // Asynchronous mode
    TXSTA1bits.TXEN = 1;  // Enable transmitter
    TXSTA1bits.TX9 = 0;   // 8-bit transmission
    RCSTA1bits.SPEN = 1;  // Enable serial port (configures RX/DT and TX/CK pins as serial port pins)
    RCSTA1bits.CREN = 1;  // Enable receiver
    RCSTA1bits.RX9 = 0;   // 8-bit reception
    TXSTA = 0x04; // 8-bit transmit, enable transmitter, asynchronous, high speed mode
    RCSTA = 0x90; // 8-bit receiver, enable receiver, serial port enabled
    BAUDCONbits.BRG16 = 0;
    SPBRGH1 = (BRGVAL >> 8) & 0xFF;
    SPBRG1 = BRGVAL & 0xFF;
}

void initInterrupts() {
    /* Configure I/O ports */
    TRISCbits.RC7 = 1; // TX and RX pin configuration
    TRISCbits.RC6 = 0;

    /* Configure the interrupts */
    INTCON = 0; // Clear interrupt register
    PIE1bits.TXIE = 1; // Enable USART transmit interrupt
    PIE1bits.RCIE = 1; // Enable USART receive interrupt
    PIR1 = 0; // Clear all peripheral flags
    INTCONbits.PEIE = 1; // Enable peripheral interrupts
    INTCONbits.GIE = 1; // Globally enable interrupts

    /* Configure Timer0 for periodic interrupts */
    T0CON = 0b10000111;  // Timer0 on, 16-bit mode, prescaler 1:256
    TMR0H = 0xF0;        // Load high byte of timer value
    TMR0L = 0xCA;        // 100 ms interrupt rate
    INTCONbits.TMR0IE = 1;
    
    // LED ports set as input
    TRISBbits.RB0 = 0;
    TRISCbits.RC0 = 0;
    TRISDbits.RD0 = 0;
    TRISAbits.RA0 = 0;
    
    TRISBbits.RB4 = 1;
    TRISBbits.RB5 = 1;
    TRISBbits.RB6 = 1;
    TRISBbits.RB7 = 1;
}

unsigned int readADC() {
    ADCON0bits.GO = 1; // Start conversion
    while (ADCON0bits.GO_nDONE); // Wait for conversion to finish
    return (ADRESH << 8) + ADRESL; // Return the 10-bit result
}

unsigned int mapAltitude(unsigned int adcValue) {
    if (adcValue >= 768) {
        return 12000;
    } else if (adcValue >= 512) {
        return 11000;
    } else if (adcValue >= 256) {
        return 10000;
    } else {
        return 9000;
    }
}

void __interrupt(high_priority) highPriorityISR(void) {
    static char command_buffer[10];
    static unsigned char command_index = 0;
    
    if (RCSTA1bits.OERR) {
        RCSTA1bits.CREN = 0;
        RCSTA1bits.CREN = 1;
    }

    if (PIR1bits.RCIF) {
        char received_char = RCREG;
        if (received_char == '$') {
            command_index = 0;
        }
        if (command_index < sizeof(command_buffer) - 1) { // Prevent buffer overflow
            command_buffer[command_index++] = received_char;
        }
        if (received_char == '#') {
            command_buffer[command_index] = '\0';
            handleCommand(command_buffer);
            command_index = 0; // Reset command index after processing
        }
        PIR1bits.RCIF = 0;
    }

    if (INTCONbits.TMR0IF) {
        INTCONbits.TMR0IF = 0;
        if (current_mode == ACTIVE) {
            updateDistance();
            if (altitude_flag) altitude_timer += 100; // Assuming timer interrupt every 100 ms
            if (altitude_flag && altitude_timer >= altitude_period) {
                altitude_timer = 0;
                sendAltitudeResponse();
            } else {
                sendDistanceResponse();
            }
        }
        TMR0H = 0xF0;        // Load high byte of timer value
        TMR0L = 0xCA;        // 100 ms interrupt rate
    }
    
    if(PIR1bits.TXIF){
        if(message_g[tx_index] != '\0') {
            TXREG = message_g[tx_index];// Transmit character
            tx_index++;
        }
        else{ // Wait until TSR is full -> 
            while(TXSTAbits.TRMT == 0);
            TXSTAbits.TXEN = 0;
            tx_index=0;
        }
    }

    if (PIR1bits.ADIF) {
        PIR1bits.ADIF = 0; // Clear ADC interrupt flag
        // Handle ADC end of conversion if needed
    }
  
    if (INTCONbits.RBIF){ // If there was PORTB<4:7> change
        INTCONbits.RBIF = 0;
        if (PORTBbits.RB4) {
            LATD = (LATD & 0xFE);
            portb_pressed = 4;
        }
        else if (PORTBbits.RB5) {
            LATC = (LATC & 0xFE);
            portb_pressed = 5;
        }
        else if (PORTBbits.RB6) {
            LATB = (LATB & 0xFE);
            portb_pressed = 6;
        }
        else if (PORTBbits.RB7) {
            LATA = (LATA & 0xFE);
            portb_pressed = 7;
        }
        sendButtonPressResponse();
    }
    
}

void main(void) {
    initADC();
    initUSART();
    initInterrupts();
    //debugPrint("Starting main loop\n");

    while (finish);
}