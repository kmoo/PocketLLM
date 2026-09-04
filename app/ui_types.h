/* app/ui_types.h — the data a screen draws. Nothing else. */
#ifndef PL_UI_TYPES_H
#define PL_UI_TYPES_H

#include <stddef.h>

typedef struct {
    int         from_user;
    const char *text;
    int         streaming;   /* draw a caret: this reply is still arriving */
} pl_message;

#endif
