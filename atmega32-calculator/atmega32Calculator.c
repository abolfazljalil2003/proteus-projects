#include <mega32.h>
#include <stdio.h>
#include <lcd.h>



void dly(unsigned int ms)
{
    unsigned int i;

    TCCR0 = 0x00;


    TCCR0 |= (1 << CS01) | (1 << CS00);

    for (i = 0; i < ms; i++)
    {
        TCNT0 = 240;
        TIFR |= (1 << TOV0);
        while (!(TIFR & (1 << TOV0)));
    }


    TCCR0 = 0x00;
}


#asm
    .equ __lcd_port=0x18
#endasm

// Keypad layout: 6 rows x 4 columns
unsigned char code[4][6] = {
    {10,  7,  8,  9, 11, 12},   //  ON_C , 7, 8 , 9 , * , /
    {13, 4, 5,  6, 14, 15},     //  +/- , 4 , 5 , 6 , - , MRC
    {16,  1, 2, 3, 17, 18},     //  % , 1 , 2 , 3 , +    , M-
    {19,  0, 20, 21, 17, 22}    //  rad , 0 , . , = , + , M+
};

// Key definitions
#define KEY_ON_C    10
#define KEY_NEG     13
#define KEY_MRC     15
#define KEY_PERCENT 16
#define KEY_M_MINUS 18
#define KEY_SQRT    19
#define KEY_DOT     20
#define KEY_M_PLUS  22
#define KEY_MULT    11
#define KEY_DIV     12
#define KEY_MINUS   14
#define KEY_PLUS    17
#define KEY_EQUAL   21
#define KEY_DEL     24  // New delete key (can assign to any unused button)

// Memory array for M+/M-/MRC
#define MAX_MEMORY_SIZE 10
float memory_array[MAX_MEMORY_SIZE];
unsigned char memory_count = 0;

// Global variables
float operand1 = 0, operand2 = 0, result = 0;
char operator = 0;
unsigned char lcd_on = 0;
unsigned char entering_num = 0;
unsigned char decimal_entered = 0;
unsigned char power_mode = 0;
char display_buffer[17];
float current_display = 0;
int decimal_places = 0;
unsigned char show_result_label = 0;

unsigned char scan_key(void)
{
    unsigned char col, data;
    unsigned char num_key = 0xFF;
    unsigned char temp = 0x70;

    for (col = 0; col < 4; col++)
    {
        PORTC = temp;
       dly(50);

        data = PIND & 0x3F;

        if (data == 0x1F) { num_key = code[col][0]; break; }
        if (data == 0x2F) { num_key = code[col][1]; break; }
        if (data == 0x37) { num_key = code[col][2]; break; }
        if (data == 0x3B) { num_key = code[col][3]; break; }
        if (data == 0x3D) { num_key = code[col][4]; break; }
        if (data == 0x3E) { num_key = code[col][5]; break; }

        temp = ((temp >> 1) | 0x80) & 0xF0;
    }

    return num_key;
}




// Function to turn LCD on/off
void toggle_lcd(void)
{
    if (lcd_on)
    {
        lcd_on = 0;
        PORTA = 0x08;
        lcd_clear();
    }
    else
    {
        lcd_on = 1;
        PORTA = 0x0C;
        lcd_init(16);
        lcd_clear();
        lcd_puts("Calculator Ready");
       dly(500);
        lcd_clear();
    }
}

// Custom function to convert float to string
void float_to_string(float num, char* buffer, unsigned char decimals)
{
    int int_part;
    float frac_part;
    int i, j;
    char temp[17];
    int is_negative = 0;
    int digit;

    if (num < 0)
    {
        is_negative = 1;
        num = -num;
    }

    int_part = (int)num;
    frac_part = num - int_part;

    i = 0;
    if (int_part == 0)
    {
        temp[i++] = '0';
    }
    else
    {
        while (int_part > 0)
        {
            temp[i++] = (int_part % 10) + '0';
            int_part /= 10;
        }
    }

    if (is_negative)
    {
        temp[i++] = '-';
    }

    j = 0;
    for (i = i - 1; i >= 0; i--)
    {
        buffer[j++] = temp[i];
    }

    if (decimals > 0)
    {
        buffer[j++] = '.';

        for (i = 0; i < decimals; i++)
        {
            frac_part *= 10;
            digit = (int)frac_part;
            buffer[j++] = digit + '0';
            frac_part -= digit;
        }
    }

    buffer[j] = '\0';
}

// Function to update display
void update_display(void)
{
    if (!lcd_on) return;

    // Clear only the second line to preserve operator on first line
    lcd_gotoxy(0, 1);
    lcd_puts("                "); // Clear line
    lcd_gotoxy(0, 1);

    if (decimal_entered)
        float_to_string(current_display, display_buffer, 4);
    else
        float_to_string(current_display, display_buffer, 0);
    lcd_puts(display_buffer);
}

// Custom absolute value function
float abs_float(float x)
{
    return (x < 0) ? -x : x;
}

// Custom square root implementation
float sqrt_custom(float x)
{
    float guess, prev_guess;
    float epsilon = 0.00001;
    int i;

    if (x < 0) return -1;
    if (x == 0) return 0;

    guess = x / 2.0;

    for (i = 0; i < 50; i++)
    {
        prev_guess = guess;
        guess = (guess + x / guess) / 2.0;

        if (abs_float(guess - prev_guess) < epsilon)
            break;
    }

    return guess;
}

// Custom power function
float power_calc(float base, float exp)
{
    float result = 1.0;
    int i;
    int exp_int;
    float exp_frac;
    int is_negative = 0;

    if (exp < 0)
    {
        is_negative = 1;
        exp = -exp;
    }

    exp_int = (int)exp;
    exp_frac = exp - exp_int;

    for (i = 0; i < exp_int; i++)
    {
        result *= base;
    }

    if (exp_frac > 0.001)
    {
        float frac_result = 1.0;

        if (abs_float(exp_frac - 0.5) < 0.01)
        {
            frac_result = sqrt_custom(base);
        }
        else if (abs_float(exp_frac - 0.25) < 0.01)
        {
            frac_result = sqrt_custom(sqrt_custom(base));
        }
        else if (abs_float(exp_frac - 0.75) < 0.01)
        {
            frac_result = sqrt_custom(base) * sqrt_custom(sqrt_custom(base));
        }
        else
        {
            frac_result = 1.0 + exp_frac * (base - 1.0);
        }

        result *= frac_result;
    }

    if (is_negative && result != 0)
    {
        result = 1.0 / result;
    }

    return result;
}

// Memory functions
void memory_add(float value)
{
    if (memory_count < MAX_MEMORY_SIZE)
    {
        memory_array[memory_count++] = value;
        lcd_clear();
        lcd_puts("M+ Added");
       dly(500);
        lcd_clear();
        update_display();
    }
    else
    {
        lcd_clear();
        lcd_puts("Memory Full!");
      dly(1000);
        lcd_clear();
        update_display();
    }
}

void memory_remove(float value)
{
    unsigned char i, j;
    unsigned char found = 0;

    // Search for value in array
    for (i = 0; i < memory_count; i++)
    {
        if (abs_float(memory_array[i] - value) < 0.0001)
        {
            found = 1;
            // Shift elements left
            for (j = i; j < memory_count - 1; j++)
            {
                memory_array[j] = memory_array[j + 1];
            }
            memory_count--;
            lcd_clear();
            lcd_puts("M- Removed");
            dly(500);
            lcd_clear();
            update_display();
            return;
        }
    }

    if (!found)
    {
        lcd_clear();
        lcd_puts("Not in Memory");
        dly(1000);
        lcd_clear();
        update_display();
    }
}

float memory_recall(void)
{
    if (memory_count > 0)
    {
        return memory_array[memory_count - 1]; // Return last added
    }
    return 0;
}

// Function to delete last digit
void delete_last_digit(void)
{
    long temp;
    if (entering_num)
    {
        if (decimal_entered && decimal_places > 0)
        {
            decimal_places--;
            // Recalculate display by removing last decimal digit
            temp = (long)(current_display * power_calc(10, decimal_places + 1));
            temp = temp / 10;
            current_display = (float)temp / power_calc(10, decimal_places);
        }
        else if (!decimal_entered)
        {
            current_display = (long)(current_display / 10);
        }
        update_display();
    }
}

// Function to perform calculation
void calculate(void)
{
    switch(operator)
    {
        case KEY_PLUS:
            result = operand1 + operand2;
            break;
        case KEY_MINUS:
            result = operand1 - operand2;
            break;
        case KEY_MULT:
            if (power_mode)
                result = power_calc(operand1, operand2);
            else
                result = operand1 * operand2;
            break;
        case KEY_DIV:
            if (operand2 != 0)
                result = operand1 / operand2;
            else
            {
                lcd_clear();
                lcd_puts("Error: Div by 0");
                dly(1000);
                result = 0;
            }
            break;
        default:
            result = operand2;
            break;
    }
    current_display = result;
    power_mode = 0;
}

void main(void)
{
    unsigned char key;

    DDRC = 0xFF;
    PORTC = 0x00;
    DDRD = 0xC0;

    lcd_on = 0;
    memory_count = 0;

    lcd_init(16);
    lcd_clear();

    while(1)
    {
        key = scan_key();

        if (key == 0xFF || !lcd_on && key != KEY_ON_C) continue;

        // Handle ON/C button
        if (key == KEY_ON_C)
        {
            toggle_lcd();
            operand1 = 0;
            operand2 = 0;
            result = 0;
            operator = 0;
            entering_num = 0;
            decimal_entered = 0;
            current_display = 0;
            decimal_places = 0;
            power_mode = 0;
            show_result_label = 0;
            continue;
        }

        // Number keys (0-9)
        if (key <= 9)
        {
            // If showing result, clear screen first
            if (show_result_label)
            {
                lcd_clear();
                show_result_label = 0;
            }

            entering_num = 1;
            if (decimal_entered)
            {
                decimal_places++;
                current_display = current_display + key / power_calc(10, decimal_places);
            }
            else
            {
                current_display = current_display * 10 + key;
            }
            update_display();
        }
        // Decimal point
        else if (key == KEY_DOT)
        {
            if (!decimal_entered)
            {
                decimal_entered = 1;
                decimal_places = 0;
                entering_num = 1;
            }
        }
        // Operations
        else if (key == KEY_PLUS || key == KEY_MINUS || key == KEY_MULT || key == KEY_DIV)
        {
            // Clear result label when continuing operations
            if (show_result_label)
            {
                lcd_clear();
                show_result_label = 0;
            }

            if (entering_num)
            {
                if (operator != 0)
                {
                    operand2 = current_display;
                    calculate();
                    operand1 = result;
                }
                else
                {
                    operand1 = current_display;
                }
            }

            // Check for ** (power operation)
            if (key == KEY_MULT && operator == KEY_MULT)
            {
                power_mode = 1;
                lcd_clear();
                lcd_gotoxy(0, 0);
                lcd_puts("x**y mode");
                lcd_gotoxy(0, 1);
                float_to_string(operand1, display_buffer, 0);
                lcd_puts(display_buffer);
            }
            else
            {
                operator = key;
                lcd_gotoxy(0, 0);
                lcd_puts("                "); // Clear first line
                lcd_gotoxy(0, 0);
                if (key == KEY_PLUS) lcd_putchar('+');
                else if (key == KEY_MINUS) lcd_putchar('-');
                else if (key == KEY_MULT) lcd_putchar('*');
                else if (key == KEY_DIV) lcd_putchar('/');
            }

            entering_num = 0;
            decimal_entered = 0;
            current_display = 0;
            decimal_places = 0;
        }
        // Equal
        else if (key == KEY_EQUAL)
        {
            if (entering_num)
            {
                operand2 = current_display;
                calculate();
                lcd_clear();
                lcd_gotoxy(0, 0);
                lcd_puts("Result:");
                show_result_label = 1;
                update_display();
                operand1 = result;
                operator = 0;
                entering_num = 0;
                decimal_entered = 0;
                current_display = result;
                decimal_places = 0;
            }
        }
        // Memory functions - New array-based implementation
        else if (key == KEY_M_PLUS)
        {
            memory_add(current_display);
        }
        else if (key == KEY_M_MINUS)
        {
            memory_remove(current_display);
        }
        else if (key == KEY_MRC)
        {
            if (memory_count > 0)
            {
                current_display = memory_recall();
                entering_num = 1;
                lcd_clear();
                lcd_gotoxy(0, 0);
                lcd_puts("MRC:");
                update_display();
            }
            else
            {
                lcd_clear();
                lcd_puts("Memory Empty");
                dly(1000);
                lcd_clear();
            }
        }
        // Special functions
        else if (key == KEY_SQRT)
        {
            if (current_display >= 0)
            {
                current_display = sqrt_custom(current_display);
                lcd_clear();
                lcd_gotoxy(0, 0);
                lcd_puts("SQRT:");
                update_display();
            }
            else
            {
                lcd_clear();
                lcd_puts("Error: Neg SQRT");
                dly(1000);
                lcd_clear();
            }
        }
        else if (key == KEY_NEG)
        {
            current_display = -current_display;
            update_display();
        }
        else if (key == KEY_PERCENT)
        {
            current_display = current_display / 100;
            update_display();
        }
        // Delete last digit - you can assign this to any unused button
        // For now, pressing % twice acts as delete
        else if (key == KEY_PERCENT)
        {
            static unsigned char percent_count = 0;
            if (percent_count == 1)
            {
                delete_last_digit();
                percent_count = 0;
            }
            else
            {
                percent_count = 1;
            }
        }

        dly(100);
    }
}