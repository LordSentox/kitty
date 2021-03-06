/*
 * cursor.c
 * Copyright (C) 2016 Kovid Goyal <kovid at kovidgoyal.net>
 *
 * Distributed under terms of the GPL3 license.
 */

#include "data-types.h"

#include <structmember.h>

static PyObject *
new(PyTypeObject *type, PyObject UNUSED *args, PyObject UNUSED *kwds) {
    Cursor *self;

    self = (Cursor *)type->tp_alloc(type, 0);
    return (PyObject*) self;
}

static void
dealloc(Cursor* self) {
    Py_TYPE(self)->tp_free((PyObject*)self);
}

#define EQ(x) (a->x == b->x)
static int __eq__(Cursor *a, Cursor *b) {
    return EQ(bold) && EQ(italic) && EQ(strikethrough) && EQ(reverse) && EQ(decoration) && EQ(fg) && EQ(bg) && EQ(decoration_fg) && EQ(x) && EQ(y) && EQ(shape) && EQ(blink);
}

static const char* cursor_names[NUM_OF_CURSOR_SHAPES] = { "NO_SHAPE", "BLOCK", "BEAM", "UNDERLINE" };

#define BOOL(x) ((x) ? Py_True : Py_False)
static PyObject *
repr(Cursor *self) {
    return PyUnicode_FromFormat(
        "Cursor(x=%u, y=%u, shape=%s, blink=%R, fg=#%08x, bg=#%08x, bold=%R, italic=%R, reverse=%R, strikethrough=%R, decoration=%d, decoration_fg=#%08x)",
        self->x, self->y, (self->shape < NUM_OF_CURSOR_SHAPES ? cursor_names[self->shape] : "INVALID"),
        BOOL(self->blink), self->fg, self->bg, BOOL(self->bold), BOOL(self->italic), BOOL(self->reverse), BOOL(self->strikethrough), self->decoration, self->decoration_fg
    );
}

void 
cursor_reset_display_attrs(Cursor *self) {
    self->bg = 0; self->fg = 0; self->decoration_fg = 0;
    self->decoration = 0; self->bold = false; self->italic = false; self->reverse = false; self->strikethrough = false;
}

void
cursor_from_sgr(Cursor *self, unsigned int *params, unsigned int count) {
#define SET_COLOR(which) \
    if (i < count) { \
        attr = params[i++];\
        switch(attr) { \
            case 5: \
                if (i < count) \
                    self->which = (params[i++] & 0xFF) << 8 | 1; \
                break; \
            case 2: \
                if (i < count - 2) { \
                    r = params[i++] & 0xFF; \
                    g = params[i++] & 0xFF; \
                    b = params[i++] & 0xFF; \
                    self->which = r << 24 | g << 16 | b << 8 | 2; \
                }\
                break; \
        } \
    } \
    break;

    unsigned int i = 0, attr;
    uint8_t r, g, b;
    if (!count) { params[0] = 0; count = 1; }
    while (i < count) {
        attr = params[i++];
        switch(attr) {
            case 0:
                cursor_reset_display_attrs(self);  break;
            case 1:
                self->bold = true;  break;
            case 3:
                self->italic = true;  break;
            case 4:
                if (i < count) { self->decoration = MIN(3, params[i]); i++; }
                else self->decoration = 1;
                break;
            case 7:
                self->reverse = true;  break;
            case 9:
                self->strikethrough = true;  break;
            case 22:
                self->bold = false;  break;
            case 23:
                self->italic = false;  break;
            case 24:
                self->decoration = 0;  break;
            case 27:
                self->reverse = false;  break;
            case 29:
                self->strikethrough = false;  break;
START_ALLOW_CASE_RANGE
            case 30 ... 37:
                self->fg = ((attr - 30) << 8) | 1;  break;
            case 38: 
                SET_COLOR(fg);
            case 39:
                self->fg = 0;  break;
            case 40 ... 47:
                self->bg = ((attr - 40) << 8) | 1;  break;
            case 48: 
                SET_COLOR(bg);
            case 49:
                self->bg = 0;  break;
            case 90 ... 97:
                self->fg = ((attr - 90 + 8) << 8) | 1;  break;
            case 100 ... 107:
                self->bg = ((attr - 100 + 8) << 8) | 1;  break;
END_ALLOW_CASE_RANGE
            case DECORATION_FG_CODE:
                SET_COLOR(decoration_fg);
            case DECORATION_FG_CODE + 1:
                self->decoration_fg = 0; break;
        }
    }
}

static inline int
color_as_sgr(char *buf, size_t sz, unsigned long val, unsigned simple_code, unsigned aix_code, unsigned complex_code) {
    switch(val & 0xff) {
        case 1:
            val >>= 8;
            if (val < 16 && simple_code) {
                return snprintf(buf, sz, "%lu;", (val < 8) ? simple_code + val : aix_code + (val - 8));
            }
            return snprintf(buf, sz, "%u:5:%lu;", complex_code, val);
        case 2:
            return snprintf(buf, sz, "%u:2:%lu:%lu:%lu;", complex_code, (val >> 24) & 0xff, (val >> 16) & 0xff, (val >> 8) & 0xff);
        default:
            return snprintf(buf, sz, "%u;", complex_code + 1);  // reset
    }
}

static inline const char*
decoration_as_sgr(uint8_t decoration) {
    switch(decoration) {
        case 1: return "4"; 
        case 2: return "4:2";  
        case 3: return "4:3";
        default: return "24";
    }
}

const char* 
cursor_as_sgr(Cursor *self, Cursor *prev) {
    static char buf[128];
#define SZ sizeof(buf) - (p - buf) - 2
#define P(fmt, ...) { p += snprintf(p, SZ, fmt ";", __VA_ARGS__); }
    char *p = buf;
    if (self->bold != prev->bold) P("%d", self->bold ? 1 : 22);
    if (self->italic != prev->italic) P("%d", self->italic ? 3 : 23);
    if (self->reverse != prev->reverse) P("%d", self->reverse ? 7 : 27);
    if (self->strikethrough != prev->strikethrough) P("%d", self->strikethrough ? 9 : 29);
    if (self->decoration != prev->decoration) P("%s", decoration_as_sgr(self->decoration));
    if (self->fg != prev->fg) p += color_as_sgr(p, SZ, self->fg, 30, 90, 38);
    if (self->bg != prev->bg) p += color_as_sgr(p, SZ, self->bg, 40, 100, 48);
    if (self->decoration_fg != prev->decoration_fg) p += color_as_sgr(p, SZ, self->decoration_fg, 0, 0, DECORATION_FG_CODE);
#undef P
#undef SZ
    if (p > buf) *(p - 1) = 0;  // remove trailing semi-colon
    *p = 0;  // ensure string is null-terminated
    return buf;
}

static PyObject *
reset_display_attrs(Cursor *self) {
#define reset_display_attrs_doc "Reset all display attributes to unset"
    cursor_reset_display_attrs(self);
    Py_RETURN_NONE;
}

void cursor_reset(Cursor *self) {
    cursor_reset_display_attrs(self);
    self->x = 0; self->y = 0;
    self->shape = NO_CURSOR_SHAPE; self->blink = false;
}

void cursor_copy_to(Cursor *src, Cursor *dest) {
#define CCY(x) dest->x = src->x;
    CCY(x); CCY(y); CCY(shape); CCY(blink); 
    CCY(bold); CCY(italic); CCY(strikethrough); CCY(reverse); CCY(decoration); CCY(fg); CCY(bg); CCY(decoration_fg); 
}

static PyObject*
copy(Cursor *self);
#define copy_doc "Create a clone of this cursor"

// Boilerplate {{{

BOOL_GETSET(Cursor, bold)
BOOL_GETSET(Cursor, italic)
BOOL_GETSET(Cursor, reverse)
BOOL_GETSET(Cursor, strikethrough)
BOOL_GETSET(Cursor, blink)

static PyMemberDef members[] = {
    {"x", T_UINT, offsetof(Cursor, x), 0, "x"},
    {"y", T_UINT, offsetof(Cursor, y), 0, "y"},
    {"shape", T_INT, offsetof(Cursor, shape), 0, "shape"},
    {"decoration", T_UBYTE, offsetof(Cursor, decoration), 0, "decoration"},
    {"fg", T_ULONG, offsetof(Cursor, fg), 0, "fg"},
    {"bg", T_ULONG, offsetof(Cursor, bg), 0, "bg"},
    {"decoration_fg", T_ULONG, offsetof(Cursor, decoration_fg), 0, "decoration_fg"},
    {NULL}  /* Sentinel */
};

static PyGetSetDef getseters[] = {
    GETSET(bold)
    GETSET(italic)
    GETSET(reverse)
    GETSET(strikethrough)
    GETSET(blink)
    {NULL}  /* Sentinel */
};

static PyMethodDef methods[] = {
    METHOD(copy, METH_NOARGS)
    METHOD(reset_display_attrs, METH_NOARGS)
    {NULL}  /* Sentinel */
};


static PyObject *
richcmp(PyObject *obj1, PyObject *obj2, int op);

PyTypeObject Cursor_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fast_data_types.Cursor",
    .tp_basicsize = sizeof(Cursor),
    .tp_dealloc = (destructor)dealloc, 
    .tp_repr = (reprfunc)repr,
    .tp_flags = Py_TPFLAGS_DEFAULT,        
    .tp_doc = "Cursors",
    .tp_richcompare = richcmp,                   
    .tp_methods = methods,
    .tp_members = members,            
    .tp_getset = getseters,
    .tp_new = new,                
};

RICHCMP(Cursor)

// }}}
 
Cursor*
cursor_copy(Cursor *self) {
    Cursor* ans;
    ans = alloc_cursor();
    if (ans == NULL) { PyErr_NoMemory(); return NULL; }
    cursor_copy_to(self, ans);
    return ans;
}

static PyObject*
copy(Cursor *self) {
    return (PyObject*)cursor_copy(self);
}

Cursor *alloc_cursor() {
    return (Cursor*)new(&Cursor_Type, NULL, NULL);
}

INIT_TYPE(Cursor)
