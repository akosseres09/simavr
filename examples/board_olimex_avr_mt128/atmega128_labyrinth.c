#undef F_CPU
#define F_CPU 16000000
#include "avr_mcu_section.h"
AVR_MCU(F_CPU, "atmega128");

#define __AVR_ATMEGA128__ 1
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdio.h>

static void port_init()
{
    PORTA = 0b00011111;
    DDRA = 0b01000000; // buttons & led
    PORTB = 0b00000000;
    DDRB = 0b00000000;
    PORTC = 0b00000000;
    DDRC = 0b11110111; // lcd
    PORTD = 0b11000000;
    DDRD = 0b00001000;
    PORTE = 0b00100000;
    DDRE = 0b00110000; // buzzer
    PORTF = 0b00000000;
    DDRF = 0b00000000;
    PORTG = 0b00000000;
    DDRG = 0b00000000;
}

#define BUTTON_NONE 0
#define BUTTON_CENTER 1
#define BUTTON_LEFT 2
#define BUTTON_RIGHT 3
#define BUTTON_UP 4
#define BUTTON_DOWN 5

static int button_accept = 1;

static int button_pressed()
{
    if (!(PINA & 0b00000001) && button_accept)
    {
        button_accept = 0;
        return BUTTON_UP;
    }
    if (!(PINA & 0b00000010) && button_accept)
    {
        button_accept = 0;
        return BUTTON_LEFT;
    }
    if (!(PINA & 0b00000100) && button_accept)
    {
        button_accept = 0;
        return BUTTON_CENTER;
    }
    if (!(PINA & 0b00001000) && button_accept)
    {
        button_accept = 0;
        return BUTTON_RIGHT;
    }
    if (!(PINA & 0b00010000) && button_accept)
    {
        button_accept = 0;
        return BUTTON_DOWN;
    }

    return BUTTON_NONE;
}

static void button_unlock()
{
    if (((PINA & 0b00000001) &&
         (PINA & 0b00000010) &&
         (PINA & 0b00000100) &&
         (PINA & 0b00001000) &&
         (PINA & 0b00010000)))
    {
        button_accept = 1;
    }
}
volatile unsigned char player_visible = 1;

ISR(TIMER1_COMPA_vect)
{
    player_visible = !player_visible;
}

#define CLR_DISP 0x00000001
#define DISP_ON 0x0000000C
#define DISP_OFF 0x00000008
#define CUR_HOME 0x00000002
#define DD_RAM_ADDR 0x00000080
#define DD_RAM_ADDR2 0x000000C0
#define CG_RAM_ADDR 0x40

static void lcd_pulse()
{
    PORTC = PORTC | 0b00000100;
    _delay_us(50);
    PORTC = PORTC & 0b11111011;
    _delay_us(50);
}

static void lcd_send(int command, unsigned char a)
{
    unsigned char data;

    data = 0b00001111 | a;
    PORTC = (PORTC | 0b11110000) & data;
    if (command)
        PORTC = PORTC & 0b11111110;
    else
        PORTC = PORTC | 0b00000001;
    lcd_pulse();

    data = a << 4;
    PORTC = (PORTC & 0b00001111) | data;
    if (command)
        PORTC = PORTC & 0b11111110;
    else
        PORTC = PORTC | 0b00000001;
    lcd_pulse();
    _delay_us(50);
}

static void lcd_send_command(unsigned char a)
{
    lcd_send(1, a);
}

static void lcd_send_data(unsigned char a)
{
    lcd_send(0, a);
}

static void lcd_init()
{
    PORTC = PORTC & 0b11111110;
    _delay_ms(50); // wait for LCD to power up

    PORTC = 0b00110000; // set D4, D5 port to 1
    lcd_pulse();        // high->low to E port (pulse)
    _delay_ms(5);

    PORTC = 0b00110000; // set D4, D5 port to 1
    lcd_pulse();        // high->low to E port (pulse)
    _delay_us(150);

    PORTC = 0b00110000; // set D4, D5 port to 1
    lcd_pulse();        // high->low to E port (pulse)
    _delay_us(150);

    PORTC = 0b00100000; // set D4 to 0, D5 port to 1
    lcd_pulse();        // high->low to E port (pulse)

    lcd_send_command(0x28);     // function set: 4 bits interface, 2 display lines, 5x8 font
    lcd_send_command(DISP_OFF); // display off, cursor off, blinking off
    lcd_send_command(CLR_DISP); // clear display
    lcd_send_command(0x06);     // entry mode set: cursor increments, display does not shift

    lcd_send_command(DISP_ON); // Turn ON Display
    lcd_send_command(CLR_DISP);
}

static void lcd_goto(int row, int col)
{
    if (row == 0)
    {
        lcd_send_command(DD_RAM_ADDR + col);
    }
    else
    {
        lcd_send_command(DD_RAM_ADDR2 + col);
    }
}

static void lcd_clear()
{
    lcd_send_command(CLR_DISP);
    _delay_ms(2);
}

static void lcd_print_char(char c)
{
    lcd_send_data(c);
}

static void lcd_print_string(const char *str)
{
    while (*str)
    {
        lcd_print_char(*str++);
    }
}

#define MAZE_WIDTH 40     // Full maze width
#define MAZE_HEIGHT 32    // Full maze height
#define DISPLAY_WIDTH 20  // Visible area width
#define DISPLAY_HEIGHT 16 // Visible area height
#define MAX_LEVELS 5
#define WALL 1
#define PATH 0

static unsigned char maze[MAZE_HEIGHT][MAZE_WIDTH];
static int player_x, player_y;
static int exit_x, exit_y;
static int start_x, start_y;
static int viewport_x, viewport_y;
static int game_won = 0;
static int level = 1;
static int total_steps = 0;
static int level_steps = 0;

static void center_viewport_on_player()
{
    viewport_x = player_x - DISPLAY_WIDTH / 2;
    viewport_y = player_y - DISPLAY_HEIGHT / 2;

    if (viewport_x < 0)
        viewport_x = 0;
    if (viewport_x > MAZE_WIDTH - DISPLAY_WIDTH)
        viewport_x = MAZE_WIDTH - DISPLAY_WIDTH;
    if (viewport_y < 0)
        viewport_y = 0;
    if (viewport_y > MAZE_HEIGHT - DISPLAY_HEIGHT)
        viewport_y = MAZE_HEIGHT - DISPLAY_HEIGHT;
}

static const char level1_pattern[] PROGMEM =
    "1111111111111111111101111111111111111111"
    "1011010001100000000000000000000001100011"
    "1001011100111111111011101111111100001001"
    "1101010110110001000001000101000101011011"
    "1000000000011101111011110000011111111111"
    "1101011110110100000000011011010000010101"
    "1111110100000111111011110001000101110101"
    "1000000001010000000000011011101111000001"
    "1111101011111111111011110011000011101101"
    "1000001110000000000001011001111110100111"
    "1010111000111111011100011100001010101101"
    "1011101110100000010001110001100000000101"
    "1010000000111101011011000100101010101101"
    "1111111111110111010010010110101011111101"
    "1000000010100000010111010100101001100001"
    "1111111010111101010001011110101100001011"
    "1100010000011001010111010000100110101111"
    "1001000101000011010100011101111100100001"
    "1111111111111011010101010000010111111011"
    "1000000000000001011101111101000000101001"
    "1111111111111011010001000001101010001111"
    "1000000000000001011101110101001110101001"
    "1110101111101011010001000101100111111011"
    "1000111000001001111011110101110000000001"
    "1011100011011011000011000100011101010101"
    "1110111011010001111001111101111001010111"
    "1000000010011100010011010000001101010001"
    "1111010110110001111010000110111101111011"
    "1000010100011011000011010010001000011111"
    "1101110101111001111001011010101101000001"
    "1001000101000011000011010010101001010101"
    "1011111111111111111111111111111111111111";

static const char level2_pattern[] PROGMEM =
    "1110111111111111111111111111111111111111"
    "1010000000011011011000101000100000000001"
    "1011110101000000000010001110001011101011"
    "1001011111011010101011111000101000111111"
    "1011010101111111111000101011111111101101"
    "1000000000000000000010100000100100101001"
    "1111111111010111110110001111110001101011"
    "1001000011111101010100111011010111101001"
    "1101111010101000000110001001000000101011"
    "1000000000000011010100100011011110000001"
    "1011101010110111111101101010001010101111"
    "1010001111110010101000101000111011111001"
    "1011011000000110000010101010000001011101"
    "1111111111110111111010111110101101010101"
    "1000101001000100000010001010111000000001"
    "1110101100010110101010100010001110110101"
    "1000000001110011111010111010100110010101"
    "1011010101100110000011100011101100111101"
    "1001111111001111111001001010000110000101"
    "1011000000011100010011011010101110101111"
    "1111111111010101000110001110101000111001"
    "1000000010000111110010100100101010011011"
    "1111111011011100000110101110101111000001"
    "1001000000000101010100100010100100011011"
    "1101110110101111110110101010111111001001"
    "1000000100100100000011101010100000011101"
    "1111010101110111111010111110111111000101"
    "1000011100011110000000001011101010010101"
    "1111010001010000101101011011000000110101"
    "1000010101111111100101000001111011110111"
    "1011010101000000001101010100010010000001"
    "1111111111111111111111111111111111111011";

static const char level3_pattern[] PROGMEM =
    "1111111111111111111001111111111111111111"
    "1000100000001110100010000000001000000001"
    "1110111111100000001111011101111101111111"
    "1000000100110111111001110111010101101001"
    "1111110110100011011100000001010100000011"
    "1000000100001000000001010100010001101001"
    "1111110111111111110101010101000100111111"
    "1101000000010101010111110101101110101101"
    "1001111111000000000001010101100100000001"
    "1101101011111010101100011111001111010101"
    "1000100000000011101001000001011000010111"
    "1010001010111001001101011011111111010001"
    "1111101110010011101101001000101010010111"
    "1010001000111011111001101010000011110001"
    "1101101010100001000011101011010110000111"
    "1101111110110111110110001001010111110001"
    "1100000000100010000011011101010100000101"
    "1101101010110111010110000101010111010101"
    "0001111110010100010010101111010010010101"
    "0101100000111111110110100101111111011101"
    "0001111110010000000010110000110000000101"
    "1100100000111110110111110110010111010101"
    "1001111010100000011100000010110110010101"
    "1100110010101110110111010110010100110101"
    "1001100110101000000001110100110101100101"
    "1100110011111101101111100101100111110101"
    "1101110110000001000000110111110110111101"
    "1001000011011101101101100100000000000101"
    "1011110111110001100111001101101011010101"
    "1011000110000101001101101111101001011101"
    "0011010011010101100000101000001011010001"
    "1111111111111111111111111111111111111111";

static const char level4_pattern[] PROGMEM =
    "1111111111111111111111111111111111111111"
    "1101101000000101101000000110101010100001"
    "1000001011010001000011011000000000101111"
    "1110100011111100011010010101101110000001"
    "1011110110001101110010111100100100110101"
    "1001011111111100011110101001101110010101"
    "1110000000000001110000001101000010111101"
    "0001101101011100111110100101101011111001"
    "1000100111110001100000110101111000010011"
    "1011110010000100111110010000100010111001"
    "1000000111010101100000110110111110001011"
    "1110110001110101110110010100010000101001"
    "1000100111100111011100110110111010111011"
    "1101110110001110000001110011110010001001"
    "1001000011101000110101000110100110111111"
    "1111110110001110010111110000110100010101"
    "1000000111111010110100000111111111000001"
    "1110110100000000011111110100001100011111"
    "1000111111111101010000010001011001000001"
    "1010100000000001010101000101001111101111"
    "1111111010111101111111101101011101100011"
    "1000000011100000100010000101000001001001"
    "1010111001001101111000101101010111011011"
    "1011100011101101010010111001010100010001"
    "1010001010001001000110001111010110110101"
    "1011011111011101101100111101011100110111"
    "1011001100001111111001100000000110100011"
    "1010011001011010110011001011010010101001"
    "1011010011111000000111111110010111111100"
    "1111011010000010110001000000110101010101"
    "1000010011010110100101101010010000010001"
    "1111111111111111111111111111111111111111";

static const char level5_pattern[] PROGMEM =
    "1111111111111111111101111111111111111111"
    "1011010011010000000000000010101101001101"
    "1001010110000111010101011110101001011001"
    "1100000011110100010101001100001101001101"
    "1111111011011110111111011001111000011001"
    "1000000000000000101000000011001101111101"
    "1011111110101010001110101001011000101101"
    "1010000000111011011000111100000010000001"
    "1111111111100001001110001001010111010101"
    "1000000000001101111011011101010110011101"
    "1101011101100101000000000101010011000101"
    "1001010000110101110111010111010111101111"
    "1101111011100101011100010100010001000001"
    "1111101110001111000001110110110101010111"
    "1010001100101000011011000010010111110101"
    "1011011001101101010001011010111100000001"
    "1000000011111111111011110010110111011011"
    "1011110110101100000001011010000010010001"
    "1110000110001001111011000010101111011011"
    "1100110011100011100001010111100001010001"
    "1001110110001110111011010010001111111011"
    "1101000111101000000001011011100010100011"
    "1101101100001101101011001010001000111001"
    "1001001001111111001010011011101010011101"
    "1011101111000000011011010001001011001001"
    "1001111101111101110000011011101001011101"
    "1111000100000000011111001011001011110001"
    "1001100011011011111000011001111000111101"
    "1100001000010000001111001011000010010101"
    "1101111101111111011000101011111011000101"
    "1000000100000000000010001000100010010101"
    "1011111111111111111111111111111111111111";

static void generate_maze()
{
    const char *pattern_ptr = "";

    switch (level)
    {
    case 1:
        pattern_ptr = level1_pattern;
        start_x = 1;
        start_y = 31;
        exit_x = 20;
        exit_y = 0;
        break;

    case 2:
        pattern_ptr = level2_pattern;
        start_x = 37;
        start_y = 31;
        exit_x = 3;
        exit_y = 0;
        break;

    case 3:
        pattern_ptr = level3_pattern;
        start_x = 0;
        start_y = 30;
        exit_x = 20;
        exit_y = 0;
        break;

    case 4:
        pattern_ptr = level4_pattern;
        start_x = 39;
        start_y = 28;
        exit_x = 0;
        exit_y = 7;
        break;

    case 5:
        pattern_ptr = level5_pattern;
        start_x = 20;
        start_y = 0;
        exit_x = 1;
        exit_y = 31;
        break;
    }

    // Copy pattern from program memory to maze array
    for (int i = 0; i < MAZE_HEIGHT; i++)
    {
        for (int j = 0; j < MAZE_WIDTH; j++)
        {
            maze[i][j] = pgm_read_byte(&pattern_ptr[i * MAZE_WIDTH + j]) - '0';
        }
    }

    if (maze[start_y][start_x] != PATH)
    {
        maze[start_y][start_x] = PATH;
    }

    if (maze[exit_y][exit_x] != PATH)
    {
        maze[exit_y][exit_x] = PATH;
    }

    player_x = start_x;
    player_y = start_y;

    center_viewport_on_player();
    level_steps = 0;
}

static unsigned char get_pixel(int px, int py)
{
    if (px == player_x && py == player_y)
    {
        return player_visible ? WALL : PATH;
    }

    return maze[py][px];
}

// Helper: Build character data for a 5x8 pixel block
static void build_char_data(unsigned char *char_data, int pixel_x_start, int pixel_y_start)
{
    for (int row = 0; row < 8; row++)
    {
        unsigned char line = 0;
        for (int col = 0; col < 5; col++)
        {
            int px = viewport_x + pixel_x_start + col;
            int py = viewport_y + pixel_y_start + row;

            if (get_pixel(px, py))
            {
                line |= (1 << (4 - col));
            }
        }
        char_data[row] = line;
    }
}

// Helper: Define a custom character in CGRAM
static void define_custom_char(int char_index, unsigned char *char_data)
{
    lcd_send_command(CG_RAM_ADDR + (char_index * 8));
    for (int i = 0; i < 8; i++)
    {
        lcd_send_data(char_data[i]);
    }
}

// Helper: Display a character at a specific position
static void display_char_at(int char_row, int char_col, int char_index)
{
    if (char_row == 0)
    {
        lcd_send_command(DD_RAM_ADDR + char_col);
    }
    else
    {
        lcd_send_command(DD_RAM_ADDR2 + char_col);
    }
    lcd_send_data(char_index);
}

// Main draw function
static void draw_maze()
{
    int char_index = 0;

    // Define all 8 custom characters
    for (int char_row = 0; char_row < 2; char_row++)
    {
        for (int char_col = 0; char_col < 4; char_col++)
        {
            unsigned char char_data[8];
            int pixel_x_start = char_col * 5;
            int pixel_y_start = char_row * 8;

            build_char_data(char_data, pixel_x_start, pixel_y_start);
            define_custom_char(char_index, char_data);
            char_index++;
        }
    }

    // Display all characters
    char_index = 0;
    for (int char_row = 0; char_row < 2; char_row++)
    {
        for (int char_col = 0; char_col < 4; char_col++)
        {
            display_char_at(char_row, char_col, char_index);
            char_index++;
        }
    }
}

// Redraw only the character containing the player
static void draw_player_char()
{
    // Calculate which character (0-7) contains the player
    int player_screen_x = player_x - viewport_x;
    int player_screen_y = player_y - viewport_y;

    int char_col = player_screen_x / 5; // Each char is 5 pixels wide
    int char_row = player_screen_y / 8; // Each char is 8 pixels tall

    // Make sure player is within visible area
    if (char_col < 0 || char_col >= 4 || char_row < 0 || char_row >= 2)
        return;

    int char_index = char_row * 4 + char_col; // calculates CGRAM slot
    int pixel_x_start = char_col * 5;         // starting pixel x within viewport
    int pixel_y_start = char_row * 8;         // starting pixel y within viewport

    unsigned char char_data[8];
    build_char_data(char_data, pixel_x_start, pixel_y_start); // Rebuild character data
    define_custom_char(char_index, char_data);                // Redefine character in CGRAM
    display_char_at(char_row, char_col, char_index);          // Redisplay character on screen
}

static void draw_stats()
{
    lcd_goto(0, 5);
    lcd_print_string("Lv:");
    lcd_print_char(level + '0');

    lcd_goto(0, 9);
    lcd_print_string(" S:");
    char steps_str[5];
    snprintf(steps_str, sizeof(steps_str), "%d", level_steps);
    lcd_print_string(steps_str);

    lcd_goto(1, 5);
    lcd_print_string("Total:");
    snprintf(steps_str, sizeof(steps_str), "%d", total_steps);
    lcd_print_string(steps_str);
}

static int can_move(int new_x, int new_y)
{
    if (new_x < 0 || new_x >= MAZE_WIDTH || new_y < 0 || new_y >= MAZE_HEIGHT)
    {
        return 0;
    }

    if (maze[new_y][new_x] == WALL)
    {
        return 0;
    }

    return 1;
}

static void move_player(int dx, int dy)
{
    int new_x = player_x + dx;
    int new_y = player_y + dy;

    if (can_move(new_x, new_y))
    {
        player_x = new_x;
        player_y = new_y;
        total_steps++;
        level_steps++;

        center_viewport_on_player();

        if (player_x == exit_x && player_y == exit_y)
        {
            game_won = 1;
        }
    }
}
static void show_win_screen()
{
    lcd_clear();
    lcd_goto(0, 0);
    lcd_print_string(" Level ");
    lcd_print_char(level + '0');
    lcd_print_string(" Clear!");
    lcd_goto(1, 0);
    lcd_print_string("Press middle btn");

    while (button_pressed() == BUTTON_CENTER)
    {
        button_unlock();
        _delay_ms(10);
    }

    while (button_pressed() != BUTTON_CENTER)
    {
        button_unlock();
        _delay_ms(10);
    }
}

static void show_start_screen()
{
    lcd_clear();
    lcd_goto(0, 0);
    lcd_print_string(" LABYRINTH GAME ");
    lcd_goto(1, 0);
    lcd_print_string("Press middle btn");

    while (button_pressed() == BUTTON_CENTER)
    {
        button_unlock();
        _delay_ms(10);
    }

    while (button_pressed() != BUTTON_CENTER)
    {
        button_unlock();
        _delay_ms(10);
    }
}

static void show_finish_screen()
{
    lcd_clear();
    lcd_goto(1, 0);
    lcd_print_string("    You Win!    ");
    lcd_goto(0, 0);
    lcd_print_string("CONGRATULATIONS!");

    while (button_pressed() == BUTTON_CENTER)
    {
        button_unlock();
        _delay_ms(10);
    }

    while (button_pressed() != BUTTON_CENTER)
    {
        button_unlock();
        _delay_ms(10);
    }
}

int main()
{
    port_init();
    lcd_init();

    TCCR1B = (4 << CS10); // 256 prescaler
    TIMSK |= (1 << OCIE1A);
    sei();
    show_start_screen();

    while (1)
    {
        game_won = 0;
        generate_maze();
        lcd_clear();

        int need_redraw = 1;
        unsigned char last_player_state = player_visible;
        while (!game_won)
        {
            // Redraw only player character if blink state changed
            if (last_player_state != player_visible)
            {
                draw_player_char();
                last_player_state = player_visible;
            }

            if (need_redraw)
            {
                draw_maze();
                draw_stats();
                need_redraw = 0;
            }

            int button = button_pressed();
            if (button != BUTTON_NONE)
            {
                switch (button)
                {
                case BUTTON_LEFT:
                    move_player(-1, 0);
                    need_redraw = 1;
                    break;
                case BUTTON_RIGHT:
                    move_player(1, 0);
                    need_redraw = 1;
                    break;
                case BUTTON_UP:
                    move_player(0, -1);
                    need_redraw = 1;
                    break;
                case BUTTON_DOWN:
                    move_player(0, 1);
                    need_redraw = 1;
                    break;
                case BUTTON_CENTER:
                    break;
                }
            }

            button_unlock();
        }

        if (level < MAX_LEVELS)
        {
            show_win_screen();
        }

        level++;
        if (level > MAX_LEVELS)
        {
            show_finish_screen();

            game_won = 0;
            level = 1;
            total_steps = 0;
        }
    }

    return 0;
}