#undef F_CPU
#define F_CPU 16000000
#include "avr_mcu_section.h"
AVR_MCU(F_CPU, "atmega128");

#define __AVR_ATMEGA128__ 1
#include <avr/io.h>
#include <avr/interrupt.h>
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
// Blink control
volatile unsigned char player_visible = 1;

// Timer1 Compare Match A interrupt - fires every 1 second
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

// GAME STATE
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

static void generate_maze()
{

    switch (level)
    {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    {
        // Reuse level 1 for now - you can expand these later
        for (int i = 0; i < MAZE_HEIGHT; i++)
        {
            for (int j = 0; j < MAZE_WIDTH; j++)
            {
                if (i == 0 || i == MAZE_HEIGHT - 1 || j == 0 || j == MAZE_WIDTH - 1)
                {
                    maze[i][j] = WALL;
                }
                else if (i % 4 == 0 && j % 6 != 1)
                {
                    maze[i][j] = WALL;
                }
                else if (j % 8 == 0 && i % 3 != 1)
                {
                    maze[i][j] = WALL;
                }
                else
                {
                    maze[i][j] = PATH;
                }
            }
        }

        start_x = 0;
        start_y = 1;
        player_x = start_x;
        player_y = start_y;
        exit_x = MAZE_WIDTH - 1;
        exit_y = MAZE_HEIGHT - 2;

        maze[start_y][start_x] = 0;
        maze[exit_y][exit_x] = 0;
        break;
    }
    }

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

        // Check if reached exit
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
        }
    }

    return 0;
}