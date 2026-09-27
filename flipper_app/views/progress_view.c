#include "progress_view.h"

#include <furi.h>
#include <gui/elements.h>

struct ProgressView {
    View* view;
};

typedef struct {
    char title[24];
    char subtitle[48];
    char status[40];
    uint32_t done;
    uint32_t total;
    bool has_progress;
} ProgressViewModel;

static void format_size(char* out, size_t out_size, uint32_t bytes) {
    if(bytes < 1024) {
        snprintf(out, out_size, "%luB", bytes);
    } else {
        snprintf(out, out_size, "%lu.%luK", bytes / 1024, (bytes % 1024) * 10 / 1024);
    }
}

static void progress_view_draw_callback(Canvas* canvas, void* _model) {
    ProgressViewModel* model = _model;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, model->title);

    canvas_set_font(canvas, FontSecondary);
    char subtitle[sizeof(model->subtitle)];
    strlcpy(subtitle, model->subtitle, sizeof(subtitle));
    // Shorten long names from the end so they fit the screen.
    size_t length = strlen(subtitle);
    while(length > 3 && canvas_string_width(canvas, subtitle) > 124) {
        subtitle[--length] = '\0';
        memcpy(&subtitle[length - 2], "..", 2);
    }
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignTop, subtitle);
    canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignTop, model->status);

    if(model->has_progress) {
        char done[12], text[32];
        format_size(done, sizeof(done), model->done);
        if(model->total > 0) {
            char total[12];
            format_size(total, sizeof(total), model->total);
            snprintf(text, sizeof(text), "%s / %s", done, total);
            float value = (float)model->done / (float)model->total;
            elements_progress_bar(canvas, 4, 40, 120, value > 1.0f ? 1.0f : value);
        } else {
            snprintf(text, sizeof(text), "%s", done);
        }
        canvas_draw_str_aligned(canvas, 64, 51, AlignCenter, AlignTop, text);
    } else {
        canvas_draw_str_aligned(canvas, 64, 51, AlignCenter, AlignTop, "Back = cancel");
    }
}

ProgressView* progress_view_alloc(void) {
    ProgressView* progress = malloc(sizeof(ProgressView));
    progress->view = view_alloc();
    view_allocate_model(progress->view, ViewModelTypeLocking, sizeof(ProgressViewModel));
    view_set_draw_callback(progress->view, progress_view_draw_callback);
    return progress;
}

void progress_view_free(ProgressView* progress) {
    furi_assert(progress);
    view_free(progress->view);
    free(progress);
}

View* progress_view_get_view(ProgressView* progress) {
    furi_assert(progress);
    return progress->view;
}

void progress_view_reset(ProgressView* progress, const char* title, const char* subtitle) {
    with_view_model(
        progress->view,
        ProgressViewModel * model,
        {
            strlcpy(model->title, title, sizeof(model->title));
            strlcpy(model->subtitle, subtitle ? subtitle : "", sizeof(model->subtitle));
            strlcpy(model->status, "Starting...", sizeof(model->status));
            model->done = 0;
            model->total = 0;
            model->has_progress = false;
        },
        true);
}

void progress_view_set_status(ProgressView* progress, const char* status) {
    with_view_model(
        progress->view,
        ProgressViewModel * model,
        { strlcpy(model->status, status, sizeof(model->status)); },
        true);
}

void progress_view_set_progress(ProgressView* progress, uint32_t done, uint32_t total) {
    with_view_model(
        progress->view,
        ProgressViewModel * model,
        {
            model->done = done;
            model->total = total;
            model->has_progress = true;
        },
        true);
}
