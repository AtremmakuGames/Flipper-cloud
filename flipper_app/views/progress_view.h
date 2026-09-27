#pragma once

#include <gui/view.h>

/** Operation screen: title, file name, status line and a progress bar. */
typedef struct ProgressView ProgressView;

ProgressView* progress_view_alloc(void);

void progress_view_free(ProgressView* progress);

View* progress_view_get_view(ProgressView* progress);

void progress_view_reset(ProgressView* progress, const char* title, const char* subtitle);

/** Thread safe, can be called from the worker. */
void progress_view_set_status(ProgressView* progress, const char* status);

/** Thread safe. `total` == 0 means the size is unknown (no bar is drawn). */
void progress_view_set_progress(ProgressView* progress, uint32_t done, uint32_t total);
