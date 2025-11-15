#include <stdio.h>
#include <unistd.h>
#include <wiringx.h>

int main() {
    int LED = 25;   
    int BTN = 21;   

    if(wiringXSetup("milkv_duo256m", NULL) == -1) {
        printf("Failed to init wiringX\n");
        wiringXGC();
        return 1;
    }

    // GPIO 模式設定
    pinMode(LED, PINMODE_OUTPUT);  
    pinMode(BTN, PINMODE_INPUT);    
    if(wiringXValidGPIO(LED) != 0) {
        printf("Invalid GPIO %d\n", LED);
    }
    if(wiringXValidGPIO(BTN) != 0) {
        printf("Invalid GPIO %d\n", BTN);
    }

    printf("Press the button to toggle LED (BTN=21, LED=25)\n");

    int led_state = 0;     
    int last_state = LOW; 
    int current_state;
    digitalWrite(LED, LOW);

    while (1) {
        current_state = digitalRead(BTN);

        if (last_state == HIGH && current_state == LOW) {
            usleep(30000);             
            if (digitalRead(BTN) == LOW) {
                led_state = !led_state;
                digitalWrite(LED, led_state ? HIGH : LOW);

                printf("Button pressed → LED = %s\n",
                       led_state ? "ON" : "OFF");
            }
        }

        last_state = current_state;
        usleep(1000);  
    }

    return 0;
}
