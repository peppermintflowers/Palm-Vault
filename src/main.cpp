//Team Members: Afsara Khan, Sohani Patki and Venesa Gomes

#include <mbed.h>
#include "drivers/LCD_DISCO_F429ZI.h"
#include "drivers/gyro.h"
#include <cmath>


static BufferedSerial serial_port(USBTX, USBRX, 115200);

FileHandle *mbed::mbed_override_console(int fd) {
    return &serial_port;
}

//Assume all keys require 5 gestures
#define moves 5

LCD_DISCO_F429ZI lcd;

//This LED glows to indicate record mode
DigitalOut record_mode(LED1);
//This LED glows to indicate enter-key mode
DigitalOut enter_key_mode(LED2);
//Press button to enter record mode (long press) or enter-key mode(short press)
InterruptIn button(BUTTON1);
//To keep track of time for which button was pressed
int press_seconds=0;
Timer press_counter;

//Configuring gyrometer and spi parameters
#define CTRL_REG1 0x20                 
#define CTRL_REG1_CONFIG 0b01'10'1'1'1'1 
#define CTRL_REG4 0x23                  
#define CTRL_REG4_CONFIG 0b0'0'01'0'00'0
#define CTRL_REG3 0x22                 
#define CTRL_REG3_CONFIG 0b0'0'0'0'1'000
#define SPI_FLAG 1                      
#define DATA_READY_FLAG 2               
#define OUT_X_L 0x28
EventFlags flags;
SPI spi(PF_9, PF_8, PF_7, PC_1, use_gpio_ssel);
uint16_t raw_gx, raw_gy, raw_gz;
float gx, gy, gz;
uint8_t write_buf[32], read_buf[32];

//Configuring gyrometer data filtering parameters
#define SCALING_FACTOR (17.5f * 0.0174532925199432957692236907684886f / 1000.0f)
#define WINDOW_SIZE 20
#define FIR_SIZE 9
float fir_coefficients[FIR_SIZE] = {0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 0.2f, 0.15f, 0.1f, 0.05f};
float fir_buffer_gx[FIR_SIZE] = {0};
float fir_buffer_gy[FIR_SIZE] = {0};
float fir_buffer_gz[FIR_SIZE] = {0};
int fir_buffer_index = 0;
float window_gx[WINDOW_SIZE] = {0}, window_gy[WINDOW_SIZE] = {0}, window_gz[WINDOW_SIZE] = {0};
int window_index = 0;

//Stores gyroscope data for move performed by user
float entered_moves[moves][3] = {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};
//The key that has been set by user
float key_moves[moves][3] = {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};
//Starting position when tracking gyrometer data
float base_position[3] = {0,0,0};
int8_t compare = -1; 
//Stores similarity index between moves performed by user and key 
float similarity = -2;
float magnitude_diff = 0;


float apply_fir_filter(float input, float buffer[], float coefficients[], int size) {
    buffer[fir_buffer_index] = input;
    float output = 0.0f;
    for (int i = 0; i < size; i++) {
        output += coefficients[i] * buffer[(fir_buffer_index - i + size) % size];
    }
    fir_buffer_index = (fir_buffer_index + 1) % size;
    return output;
}

//Sets spi flag
void spi_cb(int event)
{
    flags.set(SPI_FLAG);
}

//Sets data ready flag
void data_cb()
{
    flags.set(DATA_READY_FLAG);
}

//Computes similarity between moves performed by user and key
void compute_comparison() {
    printf("Key Moves:\n");
    for (int i = 0; i < moves; i++) {
        printf("  Move %d: gx: %4.5f, gy: %4.5f, gz: %4.5f\n", i + 1, key_moves[i][0], key_moves[i][1], key_moves[i][2]);
    }

    printf("Entered Moves:\n");
    for (int i = 0; i < moves; i++) {
        printf("  Move %d: gx: %4.5f, gy: %4.5f, gz: %4.5f\n", i + 1, entered_moves[i][0], entered_moves[i][1], entered_moves[i][2]);
    }

    float total_similarity = 0.0f;
    float magnitude_diff_sum = 0.0f;

    for (int i = 0; i < moves; i++) {
        // Compute dot product
        float dot = key_moves[i][0] * entered_moves[i][0] +
                    key_moves[i][1] * entered_moves[i][1] +
                    key_moves[i][2] * entered_moves[i][2];

        // Compute magnitudes
        float key_magnitude = std::sqrt(key_moves[i][0]*key_moves[i][0] +
                                        key_moves[i][1]*key_moves[i][1] +
                                        key_moves[i][2]*key_moves[i][2]);

        float entered_magnitude = std::sqrt(entered_moves[i][0]*entered_moves[i][0] +
                                            entered_moves[i][1]*entered_moves[i][1] +
                                            entered_moves[i][2]*entered_moves[i][2]);

        // Compute per-move similarity using the vector cosine angle
        float move_similarity = 0.0f;
        if (key_magnitude > 0.001f && entered_magnitude > 0.001f) {
            move_similarity = dot / (key_magnitude * entered_magnitude);
        }

        total_similarity += move_similarity;
        magnitude_diff_sum += std::abs(key_magnitude - entered_magnitude);
    }

    similarity = total_similarity / moves;
    magnitude_diff = magnitude_diff_sum / moves;
}

//Prints message on LCD to indicate that key should be set
void lcd_print_set_moves(){
    lcd.Clear(LCD_COLOR_WHITE);
    lcd.DisplayStringAt(0,LINE(4),(uint8_t*)"SET",CENTER_MODE);
    lcd.DisplayStringAt(0,LINE(6),(uint8_t*)"5",CENTER_MODE);
    lcd.DisplayStringAt(0,LINE(8),(uint8_t*)"moves to",CENTER_MODE);
    lcd.DisplayStringAt(0,LINE(10),(uint8_t*)"unlock",CENTER_MODE);
}

//Prints message on LCD to indicate that key should be entered
void lcd_print_enter_moves(){
    lcd.Clear(LCD_COLOR_WHITE);
    lcd.DisplayStringAt(0,LINE(4),(uint8_t*)"ENTER",CENTER_MODE);
    lcd.DisplayStringAt(0,LINE(6),(uint8_t*)"5",CENTER_MODE);
    lcd.DisplayStringAt(0,LINE(8),(uint8_t*)"moves to",CENTER_MODE);
    lcd.DisplayStringAt(0,LINE(10),(uint8_t*)"unlock",CENTER_MODE);
    
}

//Resets counter that helps measure press duration
void start_press_counter(){
    press_counter.reset();
}

//Prints sequence of instructions indicating move to be performed by user
void lcd_print_enter_move_number(int8_t moves_entered){
    lcd.Clear(LCD_COLOR_DARKYELLOW);
    if(moves_entered==0){
        lcd.DisplayStringAt(0,LINE(2),(uint8_t*)"Hold at 1st move",CENTER_MODE);
    }
    else if (moves_entered==1){
        lcd.DisplayStringAt(0,LINE(2),(uint8_t*)"Hold at 2nd move",CENTER_MODE);
    }
    else if (moves_entered==2){
        lcd.DisplayStringAt(0,LINE(2),(uint8_t*)"Hold at 3rd move",CENTER_MODE);
    }
    else if (moves_entered==3){
        lcd.DisplayStringAt(0,LINE(2),(uint8_t*)"Hold at 4th move",CENTER_MODE);
    }
    else{
        lcd.DisplayStringAt(0,LINE(2),(uint8_t*)"Hold at 5th move",CENTER_MODE);
    }
}

//Tracks and stores moves performed by user
void start_monitoring_position(int8_t moves_entered){
    write_buf[0] = OUT_X_L | 0x80 | 0x40;

    spi.transfer(write_buf, 7, read_buf, 7, spi_cb);
    flags.wait_all(SPI_FLAG);

    raw_gx = (((uint16_t)read_buf[2]) << 8) | ((uint16_t)read_buf[1]);
    raw_gy = (((uint16_t)read_buf[4]) << 8) | ((uint16_t)read_buf[3]);
    raw_gz = (((uint16_t)read_buf[6]) << 8) | ((uint16_t)read_buf[5]);

    gx = ((float)raw_gx) * SCALING_FACTOR;
    gy = ((float)raw_gy) * SCALING_FACTOR;
    gz = ((float)raw_gz) * SCALING_FACTOR;

    printf("Raw Angular Velocity -> gx: %4.5f, gy: %4.5f, gz: %4.5f\n", gx, gy, gz);
    
    window_gx[window_index] = gx;
    window_gy[window_index] = gy;
    window_gz[window_index] = gz;

    float avg_gx = 0.0f, avg_gy = 0.0f, avg_gz = 0.0f;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        avg_gx += window_gx[i];
        avg_gy += window_gy[i];
        avg_gz += window_gz[i];
    }
    avg_gx /= WINDOW_SIZE;
    avg_gy /= WINDOW_SIZE;
    avg_gz /= WINDOW_SIZE;
    window_index = (window_index + 1) % WINDOW_SIZE;

    float fir_gx = apply_fir_filter(gx, fir_buffer_gx, fir_coefficients, FIR_SIZE);
    float fir_gy = apply_fir_filter(gy, fir_buffer_gy, fir_coefficients, FIR_SIZE);
    float fir_gz = apply_fir_filter(gz, fir_buffer_gz, fir_coefficients, FIR_SIZE);

    #define HPF_MA_SIZE 5
    static float hpf_ma_gx[HPF_MA_SIZE] = {0};
    static float hpf_ma_gy[HPF_MA_SIZE] = {0};
    static float hpf_ma_gz[HPF_MA_SIZE] = {0};
    static int hpf_ma_index = 0;

    hpf_ma_gx[hpf_ma_index] = fir_gx;
    hpf_ma_gy[hpf_ma_index] = fir_gy;
    hpf_ma_gz[hpf_ma_index] = fir_gz;
    hpf_ma_index = (hpf_ma_index + 1) % HPF_MA_SIZE;

    float hpf_gx = 0, hpf_gy = 0, hpf_gz = 0;
    for (int i = 0; i < HPF_MA_SIZE; i++) {
        hpf_gx += hpf_ma_gx[i];
        hpf_gy += hpf_ma_gy[i];
        hpf_gz += hpf_ma_gz[i];
    }
    hpf_gx /= HPF_MA_SIZE;
    hpf_gy /= HPF_MA_SIZE;
    hpf_gz /= HPF_MA_SIZE;

    float threshold = 0.05f; // lowered to detect more moves

    // If setting key (compare = 0), first measurement initializes base_position
    if (moves_entered == -1 && compare == 0) {
        base_position[0] = hpf_gx;
        base_position[1] = hpf_gy;
        base_position[2] = hpf_gz;
        printf("base_position initialized (record): %f, %f, %f\n", base_position[0], base_position[1], base_position[2]);
    } else {
        // For both compare=0 (record) and compare=1 (unlock), we set moves:
        if (moves_entered >= 0 && moves_entered < moves) {
            entered_moves[moves_entered][0] = (std::abs(hpf_gx - base_position[0]) > threshold) ? hpf_gx - base_position[0] : 0;
            entered_moves[moves_entered][1] = (std::abs(hpf_gy - base_position[1]) > threshold) ? hpf_gy - base_position[1] : 0;
            entered_moves[moves_entered][2] = (std::abs(hpf_gz - base_position[2]) > threshold) ? hpf_gz - base_position[2] : 0;
            printf("base_position (during calculation): %f, %f, %f\n", base_position[0], base_position[1], base_position[2]);
            printf("entered_moves[%d]: %f, %f, %f\n", moves_entered, entered_moves[moves_entered][0], entered_moves[moves_entered][1], entered_moves[moves_entered][2]);
        }
    }

    printf("FIR -> gx: %4.5f, gy: %4.5f, gz: %4.5f\n", fir_gx, fir_gy, fir_gz);
    printf("HPF -> gx: %4.5f, gy: %4.5f, gz: %4.5f\n", hpf_gx, hpf_gy, hpf_gz);
}

//Determines mode selected by button press duration
void select_mode() {
    press_seconds = press_counter.read_ms();

    if (press_seconds >= 800) {
        compare = 0;
        record_mode = 1;
        enter_key_mode = 0;
        lcd_print_set_moves();

        lcd.Clear(LCD_COLOR_WHITE);
        lcd.SetTextColor(LCD_COLOR_RED);
        lcd.SetFont(&Font20);
        lcd.DisplayStringAt(0, LINE(4), (uint8_t*)"Record Mode", CENTER_MODE);
        lcd.SetTextColor(LCD_COLOR_BLACK); 
        wait_us(1500000);

    } else if (press_seconds < 800) {
        compare = 1;
        enter_key_mode = 1;
        record_mode = 0;
        lcd_print_enter_moves();

        lcd.Clear(LCD_COLOR_WHITE);
        lcd.SetTextColor(LCD_COLOR_ORANGE);
        lcd.DisplayStringAt(0, LINE(4), (uint8_t*)"Unlock Mode", CENTER_MODE);
        lcd.SetTextColor(LCD_COLOR_BLACK); 
        wait_us(1500000);
    }

    press_seconds = 0;
    lcd.Clear(LCD_COLOR_DARKYELLOW);
    data_cb();
}


int main() {
    //Setting up SPI communication
    spi.format(8, 3);
    spi.frequency(1'000'000);

    write_buf[0] = CTRL_REG1;
    write_buf[1] = CTRL_REG1_CONFIG;
    spi.transfer(write_buf, 2, read_buf, 2, &spi_cb);
    flags.wait_all(SPI_FLAG);

    write_buf[0] = CTRL_REG4;
    write_buf[1] = CTRL_REG4_CONFIG;
    spi.transfer(write_buf, 2, read_buf, 2, &spi_cb);
    flags.wait_all(SPI_FLAG);

    write_buf[0] = CTRL_REG3;
    write_buf[1] = CTRL_REG3_CONFIG;
    spi.transfer(write_buf, 2, read_buf, 2, &spi_cb);
    flags.wait_all(SPI_FLAG);

    write_buf[1] = 0xFF;

    //Starting press duration timer and setting up button pres interrupts
    press_counter.start();
    button.rise(&start_press_counter);
    button.fall(&select_mode);

    //Prints upon start
    lcd.DisplayStringAt(0,LINE(4),(uint8_t*)"Ready to start",CENTER_MODE);

    //To track move performed by user
    int8_t moves_entered;
    //Initially key is not set so key moves are invalid
    bool key_moves_valid = false;

    while (true) {
        //if enter-key or record mode has been selected
        if (compare == 0 || compare == 1) {
            moves_entered = 0;
            printf("compare value: %d\n", compare);
            flags.wait_all(DATA_READY_FLAG);
            lcd.DisplayStringAt(0,LINE(2),(uint8_t*)"Hold board at start",CENTER_MODE);
            wait_us(2000000);
            start_monitoring_position(-1);

            //Track the 5 move key user performs
            while (moves_entered < 5) {
                flags.clear(SPI_FLAG);
                lcd_print_enter_move_number(moves_entered);
                wait_us(2000000);
                start_monitoring_position(moves_entered);
                lcd.Clear(LCD_COLOR_DARKYELLOW);
                moves_entered++;
            }

            flags.clear(DATA_READY_FLAG);
            lcd.Clear(LCD_COLOR_BLUE);
            lcd.DisplayStringAt(0, LINE(2), (uint8_t*)"Processing", CENTER_MODE);

            //Mode specific tasks
            if (compare == 0) {
                lcd.Clear(LCD_COLOR_WHITE);
                lcd.DisplayStringAt(0, LINE(2), (uint8_t*)"Key set", CENTER_MODE);
                wait_us(2000000);
                key_moves_valid = true;
                for (int i = 0; i < moves; i++) {
                    for (int j = 0; j < 3; j++) {
                        //Setting new key
                        key_moves[i][j] = entered_moves[i][j];
                    }
                }

            } else if (compare == 1) {
                for (int i = 0; i < 3; i++) {
                    base_position[i] = 0.0f;
                }

                printf("base_position reset (unlock): %f %f, %f\n", base_position[0], base_position[1], base_position[2]);
                if (key_moves_valid) {
                    compute_comparison();
                    printf("Similarity Score: %f\n", similarity);
                    printf("Magnitude Difference: %f\n", magnitude_diff);

                    if (similarity >= 0.7) {
                        //Indicate unlock is successful
                        lcd.Clear(LCD_COLOR_GREEN);
                        lcd.DisplayStringAt(0, LINE(2), (uint8_t*)"UNLOCKED", CENTER_MODE);
                    } else {
                        //Indicate unlock is not successful
                        lcd.Clear(LCD_COLOR_RED);
                        lcd.DisplayStringAt(0, LINE(2), (uint8_t*)"TRY AGAIN", CENTER_MODE);
                    }
                    wait_us(2000000);
                    printf("**********************************************\n");
                } else {
                    //Alert user that key has not been set yet
                    lcd.Clear(LCD_COLOR_RED);
                    lcd.DisplayStringAt(0, LINE(2), (uint8_t*)"Key not set!", CENTER_MODE);
                    wait_us(2000000);
                    printf("**********************************************\n");
                }
            }
        }
    }
}
