#include <iostream>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include "button_handler.h"
#include "shared_data.h"

// Long press threshold: 2 seconds (2,000,000 microseconds)
#define LONG_PRESS_THRESHOLD_US 2000000

int ButtonHandler_Init(ButtonHandler_t *handler, int buttonPin, int ledPin) {
    if (!handler) {
        std::cerr << "Invalid handler pointer" << std::endl;
        return -1;
    }
    
    std::memset(handler, 0, sizeof(ButtonHandler_t));
    handler->buttonPin = buttonPin;
    handler->ledPin = ledPin;
    handler->pressType = BUTTON_PRESS_NONE;
    handler->longPressThreshold = LONG_PRESS_THRESHOLD_US;
    
    if (wiringXSetup("milkv_duo256m", NULL) == -1) {
        std::cerr << "Failed to init wiringX" << std::endl;
        wiringXGC();
        return -1;
    }
    
    pinMode(handler->ledPin, PINMODE_OUTPUT);
    pinMode(handler->buttonPin, PINMODE_INPUT);
    if (wiringXValidGPIO(handler->ledPin) != 0) {
        std::cerr << "Invalid GPIO " << handler->ledPin << " (LED)" << std::endl;
        return -1;
    }
    if (wiringXValidGPIO(handler->buttonPin) != 0) {
        std::cerr << "Invalid GPIO " << handler->buttonPin << " (Button)" << std::endl;
        return -1;
    }
    
    digitalWrite(handler->ledPin, LOW);
    
    handler->initialized = true;
    std::cout << "Button handler initialized (Button=" << handler->buttonPin 
              << ", LED=" << handler->ledPin << ")" << std::endl;
    
    return 0;
}

void ButtonHandler_Cleanup(ButtonHandler_t *handler) {
    if (handler && handler->initialized) {
        digitalWrite(handler->ledPin, LOW);
        wiringXGC();
        handler->initialized = false;
        std::cout << "Button handler cleaned up" << std::endl;
    }
}

ButtonPressType_t ButtonHandler_GetPressType(ButtonHandler_t *handler) {
    if (!handler) return BUTTON_PRESS_NONE;
    return handler->pressType;
}

void ButtonHandler_ClearPressType(ButtonHandler_t *handler) {
    if (handler) {
        handler->pressType = BUTTON_PRESS_NONE;
    }
}

void *ButtonHandler_ThreadRoutine(void *pHandle) {
    std::cout << "Enter button handler thread" << std::endl;
    
    ButtonHandler_t *handler = static_cast<ButtonHandler_t *>(pHandle);
    if (!handler || !handler->initialized) {
        std::cerr << "Invalid button handler" << std::endl;
        pthread_exit(nullptr);
    }
    
    int last_state = HIGH;
    int current_state;
    int led_state = 0;
    struct timeval press_start_time, press_end_time;
    unsigned long press_duration_us;
    bool button_was_pressed = false;
    
    std::cout << "Button monitoring started" << std::endl;
    std::cout << "Short press (<2s): Capture photo" << std::endl;
    std::cout << "Long press (>2s): Special function" << std::endl;
    
    while (!g_bExit) {
        current_state = digitalRead(handler->buttonPin);
        
        if (last_state == HIGH && current_state == LOW) {
            usleep(30000); 
            if (digitalRead(handler->buttonPin) == LOW) {
                gettimeofday(&press_start_time, NULL);
                button_was_pressed = true;
                std::cout << "Button pressed..." << std::endl;
            }
        }
        
        if (last_state == LOW && current_state == HIGH) {
            usleep(30000);
            
            if (digitalRead(handler->buttonPin) == HIGH && button_was_pressed) {
                gettimeofday(&press_end_time, NULL);
                button_was_pressed = false;
                
                press_duration_us = (press_end_time.tv_sec - press_start_time.tv_sec) * 1000000 +
                                   (press_end_time.tv_usec - press_start_time.tv_usec);
                
                led_state = !led_state;
                digitalWrite(handler->ledPin, led_state ? HIGH : LOW);
                
                if (press_duration_us >= handler->longPressThreshold) {
                    // long press
                    handler->pressType = BUTTON_PRESS_LONG;
                    std::cout << "=== Long Press Detected ===" << std::endl;
                    std::cout << "Duration: " << (float)press_duration_us / 1000000.0f << " seconds" << std::endl;
                    std::cout << "LED: " << (led_state ? "ON" : "OFF") << std::endl;
                    std::cout << "==========================" << std::endl;
                } else {
                    // short press
                    handler->pressType = BUTTON_PRESS_SHORT;
                    std::cout << "=== Short Press Detected ===" << std::endl;
                    std::cout << "Duration: " << (float)press_duration_us / 1000000.0f << " seconds" << std::endl;
                    std::cout << "Capture requested" << std::endl;
                    std::cout << "LED: " << (led_state ? "ON" : "OFF") << std::endl;
                    std::cout << "============================" << std::endl;
                }
            }
        }
        
        last_state = current_state;
        usleep(1000);  
    }
    
    std::cout << "Exit button handler thread" << std::endl;
    pthread_exit(nullptr);
}
