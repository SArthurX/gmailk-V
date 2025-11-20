#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <wiringx.h>

// Button press types
typedef enum {
    BUTTON_PRESS_NONE ,
    BUTTON_PRESS_SHORT,    // short press
    BUTTON_PRESS_LONG      // long press
} ButtonPressType_t;

typedef struct {
    int buttonPin;
    int ledPin;
    volatile ButtonPressType_t pressType;  // press type
    bool initialized;
    unsigned long longPressThreshold;      // long press threshold (microseconds)
} ButtonHandler_t;

// Initialize button handler
int ButtonHandler_Init(ButtonHandler_t *handler, int buttonPin, int ledPin);

// Cleanup button handler
void ButtonHandler_Cleanup(ButtonHandler_t *handler);

// Check press type
ButtonPressType_t ButtonHandler_GetPressType(ButtonHandler_t *handler);

// Clear press type flag
void ButtonHandler_ClearPressType(ButtonHandler_t *handler);

// Button listening thread
void *ButtonHandler_ThreadRoutine(void *pHandle);

#endif // BUTTON_HANDLER_H
