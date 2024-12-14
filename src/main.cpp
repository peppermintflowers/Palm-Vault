#include <mbed.h>
#include "drivers/LCD_DISCO_F429ZI.h"

#define moves 5

LCD_DISCO_F429ZI lcd;

DigitalOut record_mode(LED1);
DigitalOut enter_key_mode(LED2);
InterruptIn button(BUTTON1);

int press_seconds=0;
Timer press_counter;
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

void select_mode(){
   press_seconds= press_counter.read_ms();
   if(press_seconds<800){
        //enter_key_mode
        enter_key_mode=1;
        wait_ns(1000000000);
        enter_key_mode=0;
        lcd_print_enter_moves();
        //start spi

   }
   else{
    //record_mode
    record_mode=1;
    wait_ns(1000000000);
    record_mode=0;
    lcd_print_set_moves();
    //start spi
   }
   press_seconds=0;
}


    

int main() {


    press_counter.start();
    button.rise(&start_press_counter);
    button.fall(&select_mode);
    while (1) {
        
    }
}