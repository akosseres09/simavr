#undef F_CPU
#define F_CPU 16000000
#include "avr_mcu_section.h"
AVR_MCU(F_CPU, "atmega128");

#define __AVR_ATMEGA128__ 1
#include <avr/io.h>

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

static void delay_us(unsigned int us)
{
    volatile unsigned int i;
    for (i = 0; i < us; i++)
    {
        asm volatile("nop");
    }
}

static void delay_ms(unsigned int ms)
{
    volatile unsigned int i;
    for (i = 0; i < ms; i++)
    {
        delay_us(1000);
    }
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

#define CLR_DISP 0x00000001
#define DISP_ON 0x0000000C
#define DISP_OFF 0x00000008
#define CUR_HOME 0x00000002
#define DD_RAM_ADDR 0x00000080
#define DD_RAM_ADDR2 0x000000C0

static void lcd_pulse()
{
    PORTC = PORTC | 0b00000100;
    delay_us(50);
    PORTC = PORTC & 0b11111011;
    delay_us(50);
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
    delay_us(50);
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
    delay_ms(50); // wait for LCD to power up

    PORTC = 0b00110000; // set D4, D5 port to 1
    lcd_pulse();        // high->low to E port (pulse)
    delay_ms(5);

    PORTC = 0b00110000; // set D4, D5 port to 1
    lcd_pulse();        // high->low to E port (pulse)
    delay_us(150);

    PORTC = 0b00110000; // set D4, D5 port to 1
    lcd_pulse();        // high->low to E port (pulse)
    delay_us(150);

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
    delay_ms(2);
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

// CUSTOM CHARACTERS FOR MAZE ELEMENTS
static void lcd_define_custom_chars()
{
    // Character 0: Wall (full block)
    lcd_send_command(0x40);
    lcd_send_data(0b11111);
    lcd_send_data(0b11111);
    lcd_send_data(0b11111);
    lcd_send_data(0b11111);
    lcd_send_data(0b11111);
    lcd_send_data(0b11111);
    lcd_send_data(0b11111);
    lcd_send_data(0b11111);

    // Character 1: Player (circle)
    lcd_send_command(0x48);
    lcd_send_data(0b00000);
    lcd_send_data(0b01110);
    lcd_send_data(0b10001);
    lcd_send_data(0b10001);
    lcd_send_data(0b10001);
    lcd_send_data(0b01110);
    lcd_send_data(0b00000);
    lcd_send_data(0b00000);

    // Character 2: Exit (E with decoration)
    lcd_send_command(0x50);
    lcd_send_data(0b11111);
    lcd_send_data(0b10000);
    lcd_send_data(0b11110);
    lcd_send_data(0b10000);
    lcd_send_data(0b11111);
    lcd_send_data(0b00000);
    lcd_send_data(0b11111);
    lcd_send_data(0b11111);

    // Character 3: Start (S with decoration)
    lcd_send_command(0x58);
    lcd_send_data(0b01111);
    lcd_send_data(0b10000);
    lcd_send_data(0b01110);
    lcd_send_data(0b00001);
    lcd_send_data(0b11110);
    lcd_send_data(0b00000);
    lcd_send_data(0b11111);
    lcd_send_data(0b11111);
}

// GAME STATE
#define MAZE_WIDTH 32    // Much larger maze width
#define MAZE_HEIGHT 8    // Much larger maze height
#define DISPLAY_WIDTH 16 // LCD display width
#define DISPLAY_HEIGHT 2 // LCD display height
#define WALL_CHAR 0
#define EMPTY_CHAR ' '
#define PLAYER_CHAR 1
#define EXIT_CHAR 2
#define START_CHAR 3
#define MAX_LEVELS 5

static char maze[MAZE_HEIGHT][MAZE_WIDTH];
static int player_x, player_y;
static int exit_x, exit_y;
static int viewport_x, viewport_y;
static int game_won = 0;
static int level = 1;
static int total_steps = 0;

static void copy_maze(char maze_array[MAZE_HEIGHT][MAZE_WIDTH])
{
    for (int i = 0; i < MAZE_HEIGHT; i++)
    {
        for (int j = 0; j < MAZE_WIDTH; j++)
        {
            if (maze_array[i][j] == '#')
                maze[i][j] = WALL_CHAR;
            else if (maze_array[i][j] == 'S')
            {
                maze[i][j] = START_CHAR;
                player_x = j;
                player_y = i;
            }
            else if (maze_array[i][j] == 'E')
            {
                maze[i][j] = EXIT_CHAR;
                exit_x = j;
                exit_y = i;
            }
            else
                maze[i][j] = EMPTY_CHAR;
        }
    }
}

static void generate_maze()
{
    switch (level)
    {
    case 1:
    {
        char level1[MAZE_HEIGHT][MAZE_WIDTH] = {
            "################################",
            "# #     #       #     #       E#",
            "#   ### # ##### # ### # ##### ##",
            "### #   # #   # # # # #     # ##",
            "# # # ### # # # # # # ##### # ##",
            "#         # #     #         # ##",
            "#S####### # ############### # ##",
            "################################"};
        copy_maze(level1);
        break;
    }
    case 2:
    {
        char level2[MAZE_HEIGHT][MAZE_WIDTH] = {
            "################################",
            "##  #     #   #   #   #    #####",
            "### # ### # # # # # # # ## ## ##",
            "#     # # # #     # #   #   # ##",
            "# ##### # # ## ## ### # # # # ##",
            "# #   # #       #   # #   #   ##",
            "#S# # # ### # # ###   ##### #E##",
            "################################"};

        copy_maze(level2);
        break;
    }
    case 3:
    {
        char level3[MAZE_HEIGHT][MAZE_WIDTH] = {
            "#S##############################",
            "# #    #    ## # #   #    ##  ##",
            "# # # # # # # # # # # # # ## ###",
            "#   # # # # #     # # # # #   ##",
            "#####   # # # # # #   # #   # ##",
            "#   # # # #   # # # #   ### # ##",
            "# # # #   ### # #   ### #   # ##",
            "#########################E######"};

        copy_maze(level3);
        break;
    }
    case 4:
    {
        char level4[MAZE_HEIGHT][MAZE_WIDTH] = {
            "###S#####################   ####",
            "### #   # #   # #   #   # #  #E#",
            "# # # # # # # # # # # # # # #  #",
            "#     # #   #   # # # # #   # ##",
            "# ### # ### ### # ### # # ### ##",
            "# ###   # #   # # #   # # #   ##",
            "# # ## ###  # #   # ###     # ##",
            "######     ######   ############"};

        copy_maze(level4);
        break;
    }
    case 5:
    {
        char level5[MAZE_HEIGHT][MAZE_WIDTH] = {
            "###    #########################",
            "#   ##        #   #       #   ##",
            "# ####### # ### # ### ### # # ##",
            "  #     # #     #     #     # ##",
            " ## ### # ####### # # ##### # ##",
            "  # #   ###     # # # #   # # ##",
            "# # # #     # ####E###  #   # ##",
            "#   ####  ##   S##     ########"};

        copy_maze(level5);
        break;
    }
    }

    // Initialize viewport to center around player
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

static void update_viewport(int dx, int dy)
{
    if (dy < 0)
    {
        viewport_y = player_y - 1;
    }
    else if (dy > 0)
    {
        viewport_y = player_y;
    }

    viewport_x = player_x - DISPLAY_WIDTH / 2;

    if (viewport_x < 0)
        viewport_x = 0;
    if (viewport_x > MAZE_WIDTH - DISPLAY_WIDTH)
        viewport_x = MAZE_WIDTH - DISPLAY_WIDTH;
    if (viewport_y < 0)
        viewport_y = 0;
    if (viewport_y > MAZE_HEIGHT - DISPLAY_HEIGHT)
        viewport_y = MAZE_HEIGHT - DISPLAY_HEIGHT;

    // Check if reached exit
    if (player_x == exit_x && player_y == exit_y)
    {
        game_won = 1;
    }
}

static void draw_maze()
{
    int display_row, display_col;

    for (display_row = 0; display_row < DISPLAY_HEIGHT; display_row++)
    {
        lcd_goto(display_row, 0);
        for (display_col = 0; display_col < DISPLAY_WIDTH; display_col++)
        {
            int maze_x = viewport_x + display_col;
            int maze_y = viewport_y + display_row;

            if (maze_x >= 0 && maze_x < MAZE_WIDTH && maze_y >= 0 && maze_y < MAZE_HEIGHT)
            {
                if (maze_x == player_x && maze_y == player_y)
                {
                    lcd_print_char(PLAYER_CHAR);
                }
                else
                {
                    lcd_print_char(maze[maze_y][maze_x]);
                }
            }
            else
            {
                lcd_print_char(WALL_CHAR);
            }
        }
    }
}

static int can_move(int new_x, int new_y)
{
    if (new_x < 0 || new_x >= MAZE_WIDTH || new_y < 0 || new_y >= MAZE_HEIGHT)
    {
        return 0;
    }

    if (maze[new_y][new_x] == WALL_CHAR)
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
        update_viewport(dx, dy);
    }
}
static void show_win_screen()
{
    lcd_clear();
    lcd_goto(0, 0);
    lcd_print_string(" Level ");
    lcd_print_char(level + 48);
    lcd_print_string(" Clear!");
    lcd_goto(1, 0);
    lcd_print_string("Press middle btn");

    while (button_pressed() == BUTTON_CENTER)
    {
        button_unlock();
        delay_ms(10);
    }

    while (button_pressed() != BUTTON_CENTER)
    {
        button_unlock();
        delay_ms(10);
    }
}

void show_stats_screen()
{
    lcd_clear();
    lcd_goto(0, 0);
    lcd_print_string(" Steps: ");

    // Convert total_steps to string and display
    char steps_str[10];
    int i = 0;
    int s = total_steps;

    if (s == 0)
    {
        steps_str[i++] = '0';
    }
    else
    {
        char temp[10];
        int j = 0;
        while (s > 0)
        {
            temp[j++] = (s % 10) + '0';
            s /= 10;
        }
        while (j > 0)
        {
            steps_str[i++] = temp[--j];
        }
    }
    steps_str[i] = '\0';
    lcd_print_string(steps_str);

    while (button_pressed() == BUTTON_CENTER)
    {
        button_unlock();
        delay_ms(10);
    }

    while (button_pressed() != BUTTON_CENTER)
    {
        button_unlock();
        delay_ms(10);
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
        delay_ms(10);
    }

    while (button_pressed() != BUTTON_CENTER)
    {
        button_unlock();
        delay_ms(10);
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
        delay_ms(10);
    }

    while (button_pressed() != BUTTON_CENTER)
    {
        button_unlock();
        delay_ms(10);
    }
}

int main()
{
    port_init();
    lcd_init();
    lcd_define_custom_chars();

    show_start_screen();

    while (1)
    {
        game_won = 0;
        generate_maze();
        lcd_clear();

        int need_redraw = 1;
        while (!game_won)
        {
            if (need_redraw)
            {
                draw_maze();
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
            show_stats_screen();

            game_won = 0;
            level = 1;
        }
    }

    return 0;
}