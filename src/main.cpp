#include <mbed.h>
#include "drivers/LCD_DISCO_F429ZI.h"
#include <cmath>

#define moves 5

LCD_DISCO_F429ZI lcd;

DigitalOut record_mode(LED1);
DigitalOut enter_key_mode(LED2);
InterruptIn button(BUTTON1);

#define CTRL_REG1 0x20                  // Address of Control Register 1
#define CTRL_REG1_CONFIG 0b01'10'1'1'1'1 // Configuration for enabling gyroscope and setting data rate

#define CTRL_REG4 0x23                  // Address of Control Register 4
#define CTRL_REG4_CONFIG 0b0'0'01'0'00'0 // Configuration for setting full-scale range

#define CTRL_REG3 0x22                  // Address of Control Register 3
#define CTRL_REG3_CONFIG 0b0'0'0'0'1'000 // Enable data-ready interrupt

#define SPI_FLAG 1                      // Event flag for SPI transfer completion

// Define the Data Ready Flag
#define DATA_READY_FLAG 2               // Event flag for data-ready interrupt

// Define the address to read the X-axis lower data
#define OUT_X_L 0x28
#define OUT_X_H 0x29
#define OUT_X_L 0x2A
#define OUT_X_H 0x2B
#define OUT_X_L 0x2C
#define OUT_X_H 0x2D

#define SCALING_FACTOR (17.5f * 0.0174532925199432957692236907684886f / 1000.0f)

#define WINDOW_SIZE 10


#define FIR_SIZE 5
float fir_coefficients[FIR_SIZE] = {0.2f, 0.2f, 0.2f, 0.2f, 0.2f};
float fir_buffer[FIR_SIZE] = {0};
int fir_buffer_index = 0;

int press_seconds=0;
Timer press_counter;

EventFlags flags;

uint16_t raw_gx, raw_gy, raw_gz; // raw gyro values
float gx, gy, gz; // converted gyro values
uint8_t write_buf[32], read_buf[32];
float window_gx[WINDOW_SIZE] = {0}, window_gy[WINDOW_SIZE] = {0}, window_gz[WINDOW_SIZE] = {0};
int window_index = 0;

float entered_moves[moves][3] = {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};
float key_moves[moves][3] = {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};
float base_position[3] = {0,0,0};
float cosine_compare[moves][3]= {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};

uint8_t compare = -1;
float similarity = -2;

float fir_filter(float input) {
    fir_buffer[fir_buffer_index] = input;
    float output = 0.0f;
  for (int i = 0; i < FIR_SIZE; i++) {
        output += fir_coefficients[i] * fir_buffer[(fir_buffer_index - i + FIR_SIZE) % FIR_SIZE];
    }
    fir_buffer_index = (fir_buffer_index + 1) % FIR_SIZE;
     return output;
 }

SPI spi(PF_9, PF_8, PF_7, PC_1, use_gpio_ssel);

// Callback function to be called upon SPI transfer completion
void spi_cb(int event)
{
    // Set the SPI_FLAG to signal the main thread
    flags.set(SPI_FLAG);
}

// Interrupt callback for the data-ready signal
void data_cb()
{
    // Set the DATA_READY_FLAG to signal the main thread
    flags.set(DATA_READY_FLAG);
}

void compute_comparison(){
    uint8_t cosine_sum = 0;
    for(uint8_t i=0; i<moves; i++ ){
        for(uint8_t j=0; j<moves; j++){
            cosine_sum+= (key_moves[i][j]*entered_moves[i][j])/(std::abs(key_moves[i][j])* std::abs(entered_moves[i][j]));
        }
    }
    similarity=cosine_sum/moves*3;
}
void lcd_print_set_moves(){
    lcd.Clear(LCD_COLOR_WHITE);
    lcd.DisplayStringAt(0,LINE(4),(uint8_t*)"SET",CENTER_MODE);
    lcd.DisplayStringAt(0,LINE(6),(uint8_t*)"5",CENTER_MODE);
    lcd.DisplayStringAt(0,LINE(8),(uint8_t*)"moves to",CENTER_MODE);
    lcd.DisplayStringAt(0,LINE(10),(uint8_t*)"unlock",CENTER_MODE);
}
void lcd_print_enter_moves(){
    lcd.Clear(LCD_COLOR_WHITE);
    lcd.DisplayStringAt(0,LINE(4),(uint8_t*)"ENTER",CENTER_MODE);
    lcd.DisplayStringAt(0,LINE(6),(uint8_t*)"5",CENTER_MODE);
    lcd.DisplayStringAt(0,LINE(8),(uint8_t*)"moves to",CENTER_MODE);
    lcd.DisplayStringAt(0,LINE(10),(uint8_t*)"unlock",CENTER_MODE);
    
}
void start_press_counter(){
    press_counter.reset();
}

void lcd_print_enter_move_number(uint8_t moves_entered){
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

void start_monitoring_position(uint8_t moves_entered){
        // Prepare to read gyroscope data starting from OUT_X_L
        write_buf[0] = OUT_X_L | 0x80 | 0x40; // Read command with auto-increment for sequential registers

        // Perform SPI transfer to read 6 bytes of data (X, Y, and Z axes)
        spi.transfer(write_buf, 7, read_buf, 7, spi_cb);
        flags.wait_all(SPI_FLAG);       // Wait for SPI transfer completion

        // Convert received data into 16-bit integers for each axis
        raw_gx = (((uint16_t)read_buf[2]) << 8) | ((uint16_t)read_buf[1]);
        raw_gy = (((uint16_t)read_buf[4]) << 8) | ((uint16_t)read_buf[3]);
        raw_gz = (((uint16_t)read_buf[6]) << 8) | ((uint16_t)read_buf[5]);


        // Print raw values for debugging
        //printf("RAW -> \t\tgx: %d \t gy: %d \t gz: %d \t\n", raw_gx, raw_gy, raw_gz);

        // Print formatted data for visualization (e.g., for Teleplot)
        /*printf("$raw_gx %.2f\n", raw_gx);
        printf("$raw_gy %.2f\n", raw_gy);
        printf("$raw_gz %.2f\n", raw_gz);*/

        /*printf(">x_axis: %d \n", raw_gx);
        printf(">y_axis: %d \n", raw_gy);
        printf(">z_axis: %d \n", raw_gz);*/

        // Convert raw data to actual angular velocity using the scaling factor
        gx = ((float)raw_gx) * SCALING_FACTOR;
        gy = ((float)raw_gy) * SCALING_FACTOR;
        gz = ((float)raw_gz) * SCALING_FACTOR;

        // Print the actual angular velocity values
        //printf("Actual -> \t\tgx: %4.5f \t gy: %4.5f \t gz: %4.5f \t\n", gx, gy, gz);
       /* printf("$gx %4.5f\n", gx);
        printf("$gy %4.5f\n", gy);
        printf("$gz %4.5f\n", gz);*/

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
       /* printf("Moving Average -> gx: %4.5f, gy: %4.5f, gz: %4.5f\n", avg_gx, avg_gy, avg_gz);
        printf(">Moving Average X axis-> gx: %4.5f|g\n", avg_gx);
        printf(">Moving Average Y axis-> gy: %4.5f|g\n", avg_gy);
        printf(">Moving Average Z axis-> gz: %4.5f|g\n", avg_gz);*/

        float fir_gx = fir_filter(gx);
        float fir_gy = fir_filter(gy);
        float fir_gz = fir_filter(gz);
       /* printf("LPF FIR -> gx: %4.5f\n", fir_gx);
        printf("LPF FIR -> gy: %4.5f\n", fir_gy);
        printf("LPF FIR -> gz: %4.5f\n", fir_gz);*/

        float hpf_fir_gx = gx - fir_gx;
        float hpf_fir_gy = gx - fir_gy;
        float hpf_fir_gz = gx - fir_gz;

       /* printf("HPF FIR -> gx: %4.5f\n", hpf_fir_gx);
        printf("HPF FIR -> gy: %4.5f\n", hpf_fir_gy);
        printf("HPF FIR -> gz: %4.5f\n", hpf_fir_gz);*/

        if(moves_entered==-1){
            base_position[0]=hpf_fir_gx;
            base_position[1]=hpf_fir_gy;
            base_position[2]=hpf_fir_gz;
        }
        else{
            entered_moves[moves_entered][0]=hpf_fir_gx-base_position[0];
            entered_moves[moves_entered][1]=hpf_fir_gy-base_position[1];
            entered_moves[moves_entered][2]=hpf_fir_gz-base_position[2];
            base_position[0]=hpf_fir_gx;
            base_position[1]=hpf_fir_gy;
            base_position[2]=hpf_fir_gz;
        }
        printf("HPF FIR  -> gx: %4.5f\n",  base_position[0]);
        printf("HPF FIR  -> gy: %4.5f\n", base_position[1]);
        printf("HPF FIR -> gz: %4.5f\n",  base_position[2]);

}
void select_mode(){
   press_seconds= press_counter.read_ms();
   if(press_seconds<800){
        //enter_key_mode
        enter_key_mode=1;
        wait_ns(1000000000);
        enter_key_mode=0;
        lcd_print_enter_moves(); 
        compare=1;  
   }
   else{
    //record_mode
    record_mode=1;
    wait_ns(1000000000);
    record_mode=0;
    lcd_print_set_moves();
    compare=0;
   }
   press_seconds=0;
   lcd.Clear(LCD_COLOR_DARKYELLOW);
   data_cb();
}


int main() { // SPI pins: MOSI, MISO, SCK, and Slave Select
    // Buffers for sending and receiving data over SPI

    // SPI Data transmission format and frequency
    spi.format(8, 3);
    spi.frequency(1'000'000);

    // STEP 3: GYRO Configuration!
    // 1. Control Register 1 
    write_buf[0] = CTRL_REG1;
    write_buf[1] = CTRL_REG1_CONFIG;
    spi.transfer(write_buf, 2, read_buf, 2, &spi_cb);
    flags.wait_all(SPI_FLAG);

    // 2. Control Register 4
    write_buf[0] = CTRL_REG4;
    write_buf[1] = CTRL_REG4_CONFIG;
    spi.transfer(write_buf, 2, read_buf, 2, &spi_cb);
    flags.wait_all(SPI_FLAG);

    // 3. Control Register 3
    write_buf[0] = CTRL_REG3;
    write_buf[1] = CTRL_REG3_CONFIG;
    spi.transfer(write_buf, 2, read_buf, 2, &spi_cb);
    flags.wait_all(SPI_FLAG);
    
    write_buf[1] = 0xFF;

    
    press_counter.start();
    button.rise(&start_press_counter);
    button.fall(&select_mode);

    lcd.DisplayStringAt(0,LINE(4),(uint8_t*)"Ready to start",CENTER_MODE);

     uint8_t moves_entered=0;

    while (1) {
        flags.wait_all(DATA_READY_FLAG);
        lcd.DisplayStringAt(0,LINE(2),(uint8_t*)"Hold board at start",CENTER_MODE);
        wait_us(2000000);
        start_monitoring_position(-1);

        while(moves_entered<5){
                flags.clear(SPI_FLAG);
                lcd_print_enter_move_number(moves_entered);
                wait_us(2000000);
                start_monitoring_position(moves_entered);
                lcd.Clear(LCD_COLOR_DARKYELLOW);
                moves_entered++;
                }

      flags.clear(DATA_READY_FLAG);
      moves_entered=0;
      lcd.Clear(LCD_COLOR_BLUE);
      lcd.DisplayStringAt(0,LINE(2),(uint8_t*)"Processing",CENTER_MODE);
      moves_entered=0;
      if(compare==0){
       for(int i=0; i<moves; i++){
        for(int j=0; j<moves; j++){
            key_moves[i][j]=entered_moves[i][j];
             lcd.Clear(LCD_COLOR_WHITE);
             lcd.DisplayStringAt(0,LINE(2),(uint8_t*)"Key set",CENTER_MODE);
             wait_us(2000000);
             
        }
       }
      }
      if(compare==1){
        compute_comparison();
        if(similarity>=0.7){
            lcd.Clear(LCD_COLOR_GREEN);
             lcd.DisplayStringAt(0,LINE(2),(uint8_t*)"UNLOCKED",CENTER_MODE);
        }
        else{
            lcd.Clear(LCD_COLOR_RED);
             lcd.DisplayStringAt(0,LINE(2),(uint8_t*)"TRY AGAIN",CENTER_MODE);
        }
             wait_us(2000000);
      }
      compare=-1;
      lcd.Clear(LCD_COLOR_WHITE);
      lcd.DisplayStringAt(0,LINE(2),(uint8_t*)"Ready",CENTER_MODE);
      printf("**********************************************");
            }
      
    }