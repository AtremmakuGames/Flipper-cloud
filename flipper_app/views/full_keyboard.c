#include "full_keyboard.h"

#include <furi.h>
#include <gui/elements.h>

#define TEXT_MAX_LENGTH 96
#define HEADER_LENGTH   32

#define KEY_ROWS     3
#define KEY_COLUMNS  13
#define KEY_WIDTH    9
#define KEY_ORIGIN_X 5
#define ROW_HEIGHT   10
#define ROW_ORIGIN_Y 24
#define CONTROL_ROW  KEY_ROWS
#define TOTAL_ROWS   (KEY_ROWS + 1)

typedef enum {
    LayoutLower,
    LayoutUpper,
    LayoutSymbols,
} Layout;

static const char* const layouts[3][KEY_ROWS] = {
    [LayoutLower] = {"abcdefghijklm", "nopqrstuvwxyz", "0123456789.-_"},
    [LayoutUpper] = {"ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "0123456789.-_"},
    [LayoutSymbols] = {"!@#$%^&*()+=?", "/\\|:;'\",<>[]{", "}~`0123456789"},
};

typedef enum {
    ControlShift,
    ControlSymbols,
    ControlSpace,
    ControlDelete,
    ControlSave,
    ControlCount,
} Control;

typedef struct {
    uint8_t x;
    uint8_t width;
} ControlKey;

static const ControlKey control_keys[ControlCount] = {
    [ControlShift] = {0, 18},
    [ControlSymbols] = {19, 18},
    [ControlSpace] = {38, 33},
    [ControlDelete] = {72, 23},
    [ControlSave] = {96, 32},
};

struct FullKeyboard {
    View* view;
    char* result;
    size_t result_size;
    FullKeyboardValidator validator;
    FullKeyboardDoneCallback callback;
    void* context;
};

typedef struct {
    char header[HEADER_LENGTH];
    char text[TEXT_MAX_LENGTH + 1];
    size_t max_length;
    uint8_t row;
    uint8_t column;
    Layout layout;
    const char* error;
} FullKeyboardModel;

static uint8_t row_length(const FullKeyboardModel* model, uint8_t row) {
    if(row == CONTROL_ROW) return ControlCount;
    return strlen(layouts[model->layout][row]);
}

static uint8_t key_center_x(const FullKeyboardModel* model, uint8_t row, uint8_t column) {
    UNUSED(model);
    if(row == CONTROL_ROW) return control_keys[column].x + control_keys[column].width / 2;
    return KEY_ORIGIN_X + column * KEY_WIDTH + KEY_WIDTH / 2;
}

/** Picks the key in `row` that is horizontally closest to `x`. */
static uint8_t nearest_column(const FullKeyboardModel* model, uint8_t row, uint8_t x) {
    uint8_t best = 0;
    int best_distance = INT32_MAX;
    for(uint8_t column = 0; column < row_length(model, row); column++) {
        int distance = abs((int)key_center_x(model, row, column) - (int)x);
        if(distance < best_distance) {
            best_distance = distance;
            best = column;
        }
    }
    return best;
}

static void draw_text_field(Canvas* canvas, const FullKeyboardModel* model) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_rframe(canvas, 0, 11, 128, 12, 1);

    // Show the tail of the text that fits into the field.
    const char* visible = model->text;
    while(*visible && canvas_string_width(canvas, visible) > 120) {
        visible++;
    }
    canvas_draw_str(canvas, 2, 20, visible);
    uint8_t cursor_x = 2 + canvas_string_width(canvas, visible);
    if(cursor_x > 125) cursor_x = 125;
    canvas_draw_line(canvas, cursor_x, 13, cursor_x, 20);
}

static void draw_char_rows(Canvas* canvas, const FullKeyboardModel* model) {
    canvas_set_font(canvas, FontKeyboard);
    for(uint8_t row = 0; row < KEY_ROWS; row++) {
        const char* keys = layouts[model->layout][row];
        uint8_t top = ROW_ORIGIN_Y + row * ROW_HEIGHT;
        for(uint8_t column = 0; keys[column]; column++) {
            uint8_t left = KEY_ORIGIN_X + column * KEY_WIDTH;
            char glyph[2] = {keys[column], '\0'};
            bool selected = model->row == row && model->column == column;
            if(selected) {
                canvas_set_color(canvas, ColorBlack);
                canvas_draw_box(canvas, left, top, KEY_WIDTH, ROW_HEIGHT);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_str_aligned(
                canvas, left + KEY_WIDTH / 2 + 1, top + 1, AlignCenter, AlignTop, glyph);
            canvas_set_color(canvas, ColorBlack);
        }
    }
}

static void draw_control_row(Canvas* canvas, const FullKeyboardModel* model) {
    static const char* labels[ControlCount] = {"Aa", "#?", "space", "del", "SAVE"};
    const char* layout_labels[ControlCount] = {
        model->layout == LayoutUpper ? "aA" : "Aa",
        model->layout == LayoutSymbols ? "ab" : "#?",
        labels[ControlSpace],
        labels[ControlDelete],
        labels[ControlSave],
    };

    canvas_set_font(canvas, FontSecondary);
    uint8_t top = ROW_ORIGIN_Y + CONTROL_ROW * ROW_HEIGHT;
    for(uint8_t key = 0; key < ControlCount; key++) {
        const ControlKey* control = &control_keys[key];
        bool selected = model->row == CONTROL_ROW && model->column == key;
        if(selected) {
            canvas_draw_rbox(canvas, control->x, top, control->width, ROW_HEIGHT, 2);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, control->x, top, control->width, ROW_HEIGHT, 2);
        }
        canvas_draw_str_aligned(
            canvas,
            control->x + control->width / 2,
            top + ROW_HEIGHT / 2 + 1,
            AlignCenter,
            AlignCenter,
            layout_labels[key]);
        canvas_set_color(canvas, ColorBlack);
    }
}

static void full_keyboard_draw_callback(Canvas* canvas, void* _model) {
    FullKeyboardModel* model = _model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 8, model->error ? model->error : model->header);

    char counter[12];
    snprintf(counter, sizeof(counter), "%u", strlen(model->text));
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, counter);

    draw_text_field(canvas, model);
    draw_char_rows(canvas, model);
    draw_control_row(canvas, model);
}

static void append_char(FullKeyboardModel* model, char c) {
    size_t length = strlen(model->text);
    if(length < model->max_length) {
        model->text[length] = c;
        model->text[length + 1] = '\0';
    }
}

static void delete_char(FullKeyboardModel* model) {
    size_t length = strlen(model->text);
    if(length > 0) model->text[length - 1] = '\0';
}

/** Returns true when the user asked to save. */
static bool press_key(FullKeyboardModel* model, bool long_press) {
    if(model->row < KEY_ROWS) {
        char c = layouts[model->layout][model->row][model->column];
        if(long_press && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        append_char(model, c);
        return false;
    }

    switch(model->column) {
    case ControlShift:
        model->layout = model->layout == LayoutLower ? LayoutUpper : LayoutLower;
        break;
    case ControlSymbols:
        model->layout = model->layout == LayoutSymbols ? LayoutLower : LayoutSymbols;
        break;
    case ControlSpace:
        append_char(model, ' ');
        break;
    case ControlDelete:
        if(long_press) {
            model->text[0] = '\0';
        } else {
            delete_char(model);
        }
        break;
    case ControlSave:
        return true;
    default:
        break;
    }
    return false;
}

static void move_cursor(FullKeyboardModel* model, InputKey key) {
    uint8_t length = row_length(model, model->row);
    switch(key) {
    case InputKeyLeft:
        model->column = model->column == 0 ? length - 1 : model->column - 1;
        break;
    case InputKeyRight:
        model->column = model->column + 1 >= length ? 0 : model->column + 1;
        break;
    case InputKeyUp:
    case InputKeyDown: {
        uint8_t x = key_center_x(model, model->row, model->column);
        if(key == InputKeyUp) {
            model->row = model->row == 0 ? TOTAL_ROWS - 1 : model->row - 1;
        } else {
            model->row = model->row + 1 >= TOTAL_ROWS ? 0 : model->row + 1;
        }
        model->column = nearest_column(model, model->row, x);
        break;
    }
    default:
        break;
    }
}

static bool full_keyboard_input_callback(InputEvent* event, void* context) {
    FullKeyboard* keyboard = context;
    bool consumed = false;
    bool save = false;

    with_view_model(
        keyboard->view,
        FullKeyboardModel * model,
        {
            bool is_press = event->type == InputTypeShort || event->type == InputTypeLong ||
                            event->type == InputTypeRepeat;
            if(is_press) model->error = NULL;

            if(event->key == InputKeyBack) {
                // Short Back erases, hold Back (or Back on empty text) leaves.
                if(event->type == InputTypeShort && strlen(model->text) > 0) {
                    delete_char(model);
                    consumed = true;
                } else if(event->type == InputTypeRepeat) {
                    consumed = true;
                }
            } else if(event->key == InputKeyOk) {
                if(event->type == InputTypeShort || event->type == InputTypeLong) {
                    save = press_key(model, event->type == InputTypeLong);
                }
                consumed = true;
            } else if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
                move_cursor(model, event->key);
                consumed = true;
            }

            if(save) {
                const char* error = keyboard->validator ?
                                        keyboard->validator(model->text, keyboard->context) :
                                        NULL;
                if(error) {
                    model->error = error;
                    save = false;
                } else {
                    strlcpy(keyboard->result, model->text, keyboard->result_size);
                }
            }
        },
        true);

    if(save && keyboard->callback) keyboard->callback(keyboard->context);
    return consumed;
}

FullKeyboard* full_keyboard_alloc(void) {
    FullKeyboard* keyboard = malloc(sizeof(FullKeyboard));
    memset(keyboard, 0, sizeof(FullKeyboard));
    keyboard->view = view_alloc();
    view_set_context(keyboard->view, keyboard);
    view_allocate_model(keyboard->view, ViewModelTypeLocking, sizeof(FullKeyboardModel));
    view_set_draw_callback(keyboard->view, full_keyboard_draw_callback);
    view_set_input_callback(keyboard->view, full_keyboard_input_callback);
    return keyboard;
}

void full_keyboard_free(FullKeyboard* keyboard) {
    furi_assert(keyboard);
    view_free(keyboard->view);
    free(keyboard);
}

View* full_keyboard_get_view(FullKeyboard* keyboard) {
    furi_assert(keyboard);
    return keyboard->view;
}

void full_keyboard_setup(
    FullKeyboard* keyboard,
    const char* header,
    char* buffer,
    size_t buffer_size,
    FullKeyboardValidator validator,
    FullKeyboardDoneCallback callback,
    void* context) {
    furi_assert(keyboard);
    furi_assert(buffer);
    furi_assert(buffer_size > 1);

    keyboard->result = buffer;
    keyboard->result_size = buffer_size;
    keyboard->validator = validator;
    keyboard->callback = callback;
    keyboard->context = context;

    with_view_model(
        keyboard->view,
        FullKeyboardModel * model,
        {
            strlcpy(model->header, header, sizeof(model->header));
            model->max_length = MIN(buffer_size - 1, (size_t)TEXT_MAX_LENGTH);
            strlcpy(model->text, buffer, model->max_length + 1);
            model->row = 0;
            model->column = 0;
            model->layout = LayoutLower;
            model->error = NULL;
        },
        true);
}
