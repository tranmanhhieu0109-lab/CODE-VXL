.ORG 0X00   
RJMP MAIN

MAIN:
    ;===== INIT STACK =====
    LDI R16, HIGH(RAMEND)
    OUT SPH, R16
    LDI R16, LOW(RAMEND)
    OUT SPL, R16

    RCALL UART_INIT
    RCALL ADC_INIT

LOOP:

    ;===== START ADC =====
    LDS R16, ADCSRA
    ORI R16, (1<<ADSC)
    STS ADCSRA, R16

WAIT_ADC:
    LDS R17, ADCSRA
    SBRS R17, ADIF
    RJMP WAIT_ADC

    ;===== CLEAR FLAG =====
    ORI R17, (1<<ADIF)
    STS ADCSRA, R17

    ;===== READ ADC =====
    LDS R18, ADCL
    LDS R19, ADCH

    RCALL SEND_FRAME

    RCALL DELAY_1S

    RJMP LOOP

;================ ADC INIT =================
ADC_INIT:
    ; VREF = AVCC, ADC0
    LDI R16, (1<<REFS0)
    STS ADMUX, R16

    ; Enable ADC, Prescaler = 64
    LDI R16, (1<<ADEN)|(1<<ADPS2)|(1<<ADPS1)
    STS ADCSRA, R16
    RET

;================ UART INIT =================
UART_INIT:
    ; Baud 9600 v?i F_CPU = 8MHz
    LDI R16, HIGH(51)
    STS UBRR0H, R16
    LDI R16, LOW(51)
    STS UBRR0L, R16

    ; Enable TX
    LDI R16, (1<<TXEN0)
    STS UCSR0B, R16

    ; 8-bit, no parity
    LDI R16, (1<<UCSZ01)|(1<<UCSZ00)
    STS UCSR0C, R16
    RET

;================ UART TX =================
UART_TX:
WAIT_TX:
    LDS R17, UCSR0A
    SBRS R17, UDRE0
    RJMP WAIT_TX
    STS UDR0, R16
    RET

;================ SEND FRAME =================
SEND_FRAME:
    ; Start byte
    LDI R16, 0x55
    RCALL UART_TX

    ; High byte
    MOV R16, R19
    RCALL UART_TX

    ; Low byte
    MOV R16, R18
    RCALL UART_TX

    ; End byte
    LDI R16, 0xFF
    RCALL UART_TX
    RET

;================ DELAY ~1s =================
DELAY_1S:
    LDI R20, 100
D1:
    LDI R21, 200
D2:
    LDI R22, 200
D3:
    DEC R22
    BRNE D3
    DEC R21
    BRNE D2
    DEC R20
    BRNE D1
    RET