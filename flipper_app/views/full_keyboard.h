#pragma once

#include <gui/view.h>

/**
 * Text input with the full printable ASCII set (lower/upper case letters,
 * digits and all symbols). The stock TextInput has no symbols and
 * capitalizes the first letter, which does not work for WiFi passwords.
 *
 * Controls: arrows move, OK types, hold OK types the upper case letter,
 * Back deletes a character, hold Back (or Back on empty text) leaves.
 */
typedef struct FullKeyboard FullKeyboard;

typedef void (*FullKeyboardDoneCallback)(void* context);

/** Returns an error message to show, or NULL when the text is acceptable. */
typedef const char* (*FullKeyboardValidator)(const char* text, void* context);

FullKeyboard* full_keyboard_alloc(void);

void full_keyboard_free(FullKeyboard* keyboard);

View* full_keyboard_get_view(FullKeyboard* keyboard);

/**
 * Prepares the keyboard. `buffer` holds the initial text and receives the
 * result when the user selects "Save" and the validator accepts it.
 */
void full_keyboard_setup(
    FullKeyboard* keyboard,
    const char* header,
    char* buffer,
    size_t buffer_size,
    FullKeyboardValidator validator,
    FullKeyboardDoneCallback callback,
    void* context);
