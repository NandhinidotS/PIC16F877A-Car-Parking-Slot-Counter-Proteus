/*
 * Car Parking Slot Counter
 * PIC16F877A, 4 MHz clock on OSC1, LM016L parallel LCD
 * SW1 = reset (MCLR, hardware)
 * SW2 = entry, RB0/INT falling edge
 * SW3 = exit,  RB4 PORTB change, falling edge
 * LCD: RS=RD2, E=RD3, D4-D7=RD4-RD7 (4-bit)
 * LEDs: red RC0 (full), green RC1 (free)
 */
#include <pic.h>

__CONFIG(FOSC_XT & WDTE_OFF & PWRTE_ON & BOREN_OFF & LVP_OFF & CPD_OFF & WRT_OFF & CP_OFF);

#define _XTAL_FREQ  4000000

#define LCD_RS      RD2
#define LCD_E       RD3

#define TOTAL_SLOTS 10
#define RED_LED     RC0
#define GREEN_LED   RC1

/* Events shown on the LCD second line */
#define EV_NONE     0
#define EV_IN       1
#define EV_OUT      2
#define EV_FULL     3
#define EV_EMPTY    4

static volatile unsigned char entry_flag = 0;
static volatile unsigned char exit_flag  = 0;
static unsigned char rb4_last = 1;      /* used only in the ISR */

/* ---------- Interrupt: only sets flags ---------- */

void interrupt isr(void)
{
    unsigned char p;

    if (INTF) {                         /* entry button, RB0/INT */
        INTF = 0;
        entry_flag = 1;
    }

    if (RBIF) {                         /* exit button, RB4 */
        p = PORTB;                      /* reading PORTB clears the mismatch */
        RBIF = 0;
        if (rb4_last == 1 && ((p & 0x10) == 0))
            exit_flag = 1;              /* falling edge on RB4 */
        rb4_last = (p >> 4) & 1;
    }
}

/* ---------- Parallel LCD driver (4-bit) ---------- */

static void lcd_nibble(unsigned char n)
{
    /* D4-D7 = RD4-RD7, so the high nibble goes straight to PORTD */
    PORTD = (PORTD & 0x0F) | (n & 0xF0);
    LCD_E = 1;
    __delay_us(5);
    LCD_E = 0;
    __delay_us(50);
}

static void lcd_send(unsigned char b, unsigned char rs)
{
    LCD_RS = rs;
    lcd_nibble(b & 0xF0);
    lcd_nibble((unsigned char)(b << 4));
}

static void lcd_cmd(unsigned char c)
{
    lcd_send(c, 0);
    if (c == 0x01 || c == 0x02)
        __delay_ms(2);
}

static void lcd_char(unsigned char c)
{
    lcd_send(c, 1);
}

static void lcd_goto(unsigned char row, unsigned char col)
{
    lcd_cmd(0x80 | ((row ? 0x40 : 0x00) + col));
}

static void lcd_init(void)
{
    __delay_ms(50);
    LCD_RS = 0;
    LCD_E = 0;
    lcd_nibble(0x30); __delay_ms(5);
    lcd_nibble(0x30); __delay_us(150);
    lcd_nibble(0x30); __delay_us(150);
    lcd_nibble(0x20);               /* switch to 4-bit mode */
    lcd_cmd(0x28);                  /* 2 lines, 5x8 font */
    lcd_cmd(0x0C);                  /* display on, cursor off */
    lcd_cmd(0x06);                  /* auto increment */
    lcd_cmd(0x01);                  /* clear */
    __delay_ms(2);
}

/* ---------- Text helpers (build a line in a buffer) ---------- */

static unsigned char put_str(char *dst, const char *src)
{
    unsigned char n = 0;
    while (*src)
        dst[n++] = *src++;
    return n;
}

static unsigned char put_num(char *dst, unsigned char v)
{
    unsigned char n = 0;
    if (v >= 10)
        dst[n++] = '0' + (v / 10) % 10;
    dst[n++] = '0' + (v % 10);
    return n;
}

/* Writes exactly 16 characters to one LCD row, padding with spaces */
static void lcd_row(unsigned char row, const char *buf, unsigned char n)
{
    unsigned char i;
    lcd_goto(row, 0);
    for (i = 0; i < 16; i++)
        lcd_char(i < n ? (unsigned char)buf[i] : ' ');
}

/* ---------- Display and LEDs ---------- */

static void show_status(unsigned char occupied, unsigned char event)
{
    char line[17];
    unsigned char n;
    unsigned char free_slots = TOTAL_SLOTS - occupied;

    /* Row 0: free slots, always shown */
    n = put_str(line, "Slots free: ");
    n += put_num(line + n, free_slots);
    lcd_row(0, line, n);

    /* Row 1: last event, plus cars in the lot */
    switch (event) {
    case EV_IN:    n = put_str(line, "CAR IN  -> "); break;
    case EV_OUT:   n = put_str(line, "CAR OUT -> "); break;
    case EV_FULL:  n = put_str(line, "LOT FULL!  "); break;
    case EV_EMPTY: n = put_str(line, "LOT EMPTY! "); break;
    default:       n = put_str(line, "Cars in lot: "); break;
    }
    n += put_num(line + n, occupied);
    if (event == EV_IN || event == EV_OUT)
        n += put_str(line + n, " in");
    lcd_row(1, line, n);
}

static void update_leds(unsigned char occupied)
{
    if (occupied >= TOTAL_SLOTS) {  /* lot full: red on, green off */
        RED_LED = 1;
        GREEN_LED = 0;
    } else {                        /* slots free: green on, red off */
        RED_LED = 0;
        GREEN_LED = 1;
    }
}

static void led_test(void)
{
    RED_LED = 1;   GREEN_LED = 0;  __delay_ms(500);
    RED_LED = 0;   GREEN_LED = 1;  __delay_ms(500);
    RED_LED = 0;   GREEN_LED = 0;
}

/* ---------- main ---------- */

void main(void)
{
    unsigned char occupied = 0;
    unsigned char event = EV_NONE;

    ADCON1 = 0x06;          /* all pins digital */

    TRISB0 = 1;             /* entry button input */
    TRISB4 = 1;             /* exit button input */
    TRISC0 = 0;             /* red LED output */
    TRISC1 = 0;             /* green LED output */
    TRISD = 0x00;           /* LCD outputs */
    PORTD = 0x00;
    PORTC = 0x00;

    lcd_init();
    led_test();             /* both LEDs blink once to confirm wiring */

    show_status(occupied, EV_NONE);
    update_leds(occupied);

    rb4_last = (PORTB >> 4) & 1;

    INTEDG = 0;             /* RB0/INT on falling edge */
    INTF = 0;
    INTE = 1;
    RBIF = 0;
    RBIE = 1;               /* PORTB change interrupt for RB4 */
    GIE = 1;

    while (1) {
        if (entry_flag) {
            entry_flag = 0;
            if (occupied < TOTAL_SLOTS) {
                occupied++;
                event = EV_IN;
            } else {
                event = EV_FULL;
            }
            show_status(occupied, event);
            update_leds(occupied);
        }

        if (exit_flag) {
            exit_flag = 0;
            if (occupied > 0) {
                occupied--;
                event = EV_OUT;
            } else {
                event = EV_EMPTY;
            }
            show_status(occupied, event);
            update_leds(occupied);
        }
    }
}
