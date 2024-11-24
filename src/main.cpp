#include <mbed.h>

DigitalOut record_mode(LED1);
DigitalOut enter_key_mode(LED2);
InterruptIn button(BUTTON1);

int press_seconds=0;
Timer press_counter;
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
   }
   else{
    //record_mode
    record_mode=1;
    wait_ns(1000000000);
    record_mode=0;
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