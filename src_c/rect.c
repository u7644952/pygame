/*
  pygame - Python Game Library
  Copyright (C) 2000-2001  Pete Shinners

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  Pete Shinners
  pete@shinners.org
*/

/*
 *  Python Rect Object -- useful 2d rectangle class
 */
#define PYGAMEAPI_RECT_INTERNAL
#include "pygame.h"

#include "doc/rect_doc.h"

#include "structmember.h"

#include "pgcompat.h"

#include <limits.h>

#if (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L) && \
    !defined(round)
#define pg_round(d) (((d < 0) ? (ceil((d) - 0.5)) : (floor((d) + 0.5))))
#else
#define pg_round(d) round(d)
#endif

static PyTypeObject pgRect_Type;
#define pgRect_Check(x) (PyObject_IsInstance(x, (PyObject *)&pgRect_Type))
#define pgRect_CheckExact(x) (Py_TYPE(x) == &pgRect_Type)

static int
pg_rect_init(pgRectObject *, PyObject *, PyObject *);

/* We store some rect objects which have been allocated already.
   Mostly to work around an old pypy cpyext performance issue.
*/
#ifdef PYPY_VERSION
#define PG_RECT_NUM 49152
const int PG_RECT_FREELIST_MAX = PG_RECT_NUM;
static pgRectObject *pg_rect_freelist[PG_RECT_NUM];
int pg_rect_freelist_num = -1;
#endif

/* Helper method to extract 4 ints from an object.
 *
 * This sequence extraction supports the following formats:
 *     - 4 ints
 *     - 2 tuples/lists of 2 ints each
 *
 * Params:
 *     obj: sequence object to extract the 4 ints from
 *     val1 .. val4: extracted int values
 *
 * Returns:
 *     int: 0 to indicate failure (exception set)
 *          1 to indicate success
 *
 * Assumptions:
 *     - obj argument is a sequence
 *     - all val arguments are valid pointers
 */
static int
four_ints_from_obj(PyObject *obj, int *val1, int *val2, int *val3, int *val4)
{
    Py_ssize_t length = PySequence_Length(obj);

    if (length < -1) {
        return 0; /* Exception already set. */
    }

    if (length == 2) {
        /* Get one end of the line. */
        PyObject *item = PySequence_GetItem(obj, 0);
        int result;

        if (item == NULL) {
            return 0; /* Exception already set. */
        }

        result = pg_TwoIntsFromObj(item, val1, val2);
        Py_DECREF(item);

        if (!result) {
            PyErr_SetString(PyExc_TypeError,
                            "number pair expected for first argument");
            return 0;
        }

        /* Get the other end of the line. */
        item = PySequence_GetItem(obj, 1);

        if (item == NULL) {
            return 0; /* Exception already set. */
        }

        result = pg_TwoIntsFromObj(item, val3, val4);
        Py_DECREF(item);

        if (!result) {
            PyErr_SetString(PyExc_TypeError,
                            "number pair expected for second argument");
            return 0;
        }
    }
    else if (length == 4) {
        if (!pg_IntFromObjIndex(obj, 0, val1)) {
            PyErr_SetString(PyExc_TypeError,
                            "number expected for first argument");
            return 0;
        }

        if (!pg_IntFromObjIndex(obj, 1, val2)) {
            PyErr_SetString(PyExc_TypeError,
                            "number expected for second argument");
            return 0;
        }

        if (!pg_IntFromObjIndex(obj, 2, val3)) {
            PyErr_SetString(PyExc_TypeError,
                            "number expected for third argument");
            return 0;
        }

        if (!pg_IntFromObjIndex(obj, 3, val4)) {
            PyErr_SetString(PyExc_TypeError,
                            "number expected for fourth argument");
            return 0;
        }
    }
    else {
        PyErr_Format(PyExc_TypeError,
                     "sequence argument takes 2 or 4 items (%ld given)",
                     length);
        return 0;
    }

    return 1;
}

static PyObject *
_pg_rect_subtype_new4(PyTypeObject *type, int x, int y, int w, int h)
{
    pgRectObject *rect = (pgRectObject *)type->tp_new(type, NULL, NULL);

    if (rect) {
        rect->r.x = x;
        rect->r.y = y;
        rect->r.w = w;
        rect->r.h = h;
    }
    return (PyObject *)rect;
}

static PyObject *
pg_rect_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    pgRectObject *self;

#ifdef PYPY_VERSION
    /* Only instances of the base pygame.Rect class are allowed in the
     * current freelist implementation (subclasses are not allowed) */
    if (pg_rect_freelist_num > -1 && type == &pgRect_Type) {
        self = pg_rect_freelist[pg_rect_freelist_num];
        Py_INCREF(self);
        /* This is so that pypy garbage collector thinks it is a new obj
           TODO: May be a hack. Is a hack.
           See https://github.com/pygame/pygame/issues/430
        */
        ((PyObject *)(self))->ob_pypy_link = 0;
        pg_rect_freelist_num--;
    }
    else {
        self = (pgRectObject *)type->tp_alloc(type, 0);
    }
#else
    self = (pgRectObject *)type->tp_alloc(type, 0);
#endif

    if (self != NULL) {
        self->r.x = self->r.y = 0;
        self->r.w = self->r.h = 0;
        self->weakreflist = NULL;
    }
    return (PyObject *)self;
}

/* object type functions */
static void
pg_rect_dealloc(pgRectObject *self)
{
    if (self->weakreflist != NULL) {
        PyObject_ClearWeakRefs((PyObject *)self);
    }

#ifdef PYPY_VERSION
    /* Only instances of the base pygame.Rect class are allowed in the
     * current freelist implementation (subclasses are not allowed) */
    if (pg_rect_freelist_num < PG_RECT_FREELIST_MAX - 1 &&
        pgRect_CheckExact(self)) {
        pg_rect_freelist_num++;
        pg_rect_freelist[pg_rect_freelist_num] = self;
    }
    else {
        Py_TYPE(self)->tp_free((PyObject *)self);
    }
#else
    Py_TYPE(self)->tp_free((PyObject *)self);
#endif
}

static SDL_Rect *
pgRect_FromObject(PyObject *obj, SDL_Rect *temp)
{
    int val;
    Py_ssize_t length;

    if (pgRect_Check(obj)) {
        return &((pgRectObject *)obj)->r;
    }
    if (PySequence_Check(obj) && (length = PySequence_Length(obj)) > 0) {
        if (length == 4) {
            if (!pg_IntFromObjIndex(obj, 0, &val)) {
                return NULL;
            }
            temp->x = val;
            if (!pg_IntFromObjIndex(obj, 1, &val)) {
                return NULL;
            }
            temp->y = val;
            if (!pg_IntFromObjIndex(obj, 2, &val)) {
                return NULL;
            }
            temp->w = val;
            if (!pg_IntFromObjIndex(obj, 3, &val)) {
                return NULL;
            }
            temp->h = val;
            return temp;
        }
        if (length == 2) {
            PyObject *sub = PySequence_GetItem(obj, 0);
            if (!sub || !PySequence_Check(sub) ||
                PySequence_Length(sub) != 2) {
                PyErr_Clear();
                Py_XDECREF(sub);
                return NULL;
            }
            if (!pg_IntFromObjIndex(sub, 0, &val)) {
                Py_DECREF(sub);
                return NULL;
            }
            temp->x = val;
            if (!pg_IntFromObjIndex(sub, 1, &val)) {
                Py_DECREF(sub);
                return NULL;
            }
            temp->y = val;
            Py_DECREF(sub);

            sub = PySequence_GetItem(obj, 1);
            if (sub == NULL || !PySequence_Check(sub) ||
                PySequence_Length(sub) != 2) {
                PyErr_Clear();
                Py_XDECREF(sub);
                return NULL;
            }
            if (!pg_IntFromObjIndex(sub, 0, &val)) {
                Py_DECREF(sub);
                return NULL;
            }
            temp->w = val;
            if (!pg_IntFromObjIndex(sub, 1, &val)) {
                Py_DECREF(sub);
                return NULL;
            }
            temp->h = val;
            Py_DECREF(sub);
            return temp;
        }
        if (PyTuple_Check(obj) && length == 1) /*looks like an arg?*/ {
            PyObject *sub = PyTuple_GET_ITEM(obj, 0);
            if (sub) {
                return pgRect_FromObject(sub, temp);
            }
        }
    }
    if (PyObject_HasAttrString(obj, "rect")) {
        PyObject *rectattr;
        SDL_Rect *returnrect;
        rectattr = PyObject_GetAttrString(obj, "rect");
        if (rectattr == NULL) {
            PyErr_Clear();
            return NULL;
        }
        if (PyCallable_Check(rectattr)) /*call if it's a method*/
        {
            PyObject *rectresult = PyObject_CallObject(rectattr, NULL);
            Py_DECREF(rectattr);
            if (rectresult == NULL) {
                PyErr_Clear();
                return NULL;
            }
            rectattr = rectresult;
        }
        returnrect = pgRect_FromObject(rectattr, temp);
        Py_DECREF(rectattr);
        return returnrect;
    }
    return NULL;
}

static PyObject *
pgRect_New(SDL_Rect *r)
{
    return _pg_rect_subtype_new4(&pgRect_Type, r->x, r->y, r->w, r->h);
}

static PyObject *
pgRect_New4(int x, int y, int w, int h)
{
    return _pg_rect_subtype_new4(&pgRect_Type, x, y, w, h);
}

static void
pgRect_Normalize(SDL_Rect *rect)
{
    if (rect->w < 0) {
        rect->x += rect->w;
        rect->w = -rect->w;
    }

    if (rect->h < 0) {
        rect->y += rect->h;
        rect->h = -rect->h;
    }
}

static int
_pg_do_rects_intersect(SDL_Rect *A, SDL_Rect *B)
{
    if (A->w == 0 || A->h == 0 || B->w == 0 || B->h == 0) {
        // zero sized rects should not collide with anything #1197
        return 0;
    }

    // A.left   < B.right  &&
    // A.top    < B.bottom &&
    // A.right  > B.left   &&
    // A.bottom > B.top
    return (MIN(A->x, A->x + A->w) < MAX(B->x, B->x + B->w) &&
            MIN(A->y, A->y + A->h) < MAX(B->y, B->y + B->h) &&
            MAX(A->x, A->x + A->w) > MIN(B->x, B->x + B->w) &&
            MAX(A->y, A->y + A->h) > MIN(B->y, B->y + B->h));
}

static PyObject *
pg_rect_normalize(pgRectObject *self, PyObject *_null)
{
    pgRect_Normalize(&pgRect_AsRect(self));

    Py_RETURN_NONE;
}

static PyObject *
pg_rect_move(pgRectObject *self, PyObject *args)
{
    int x = 0, y = 0;

    if (!pg_TwoIntsFromObj(args, &x, &y)) {
        return RAISE(PyExc_TypeError, "argument must contain two numbers");
    }

    return _pg_rect_subtype_new4(Py_TYPE(self), self->r.x + x, self->r.y + y,
                                 self->r.w, self->r.h);
}

static PyObject *
pg_rect_move_ip(pgRectObject *self, PyObject *args)
{
    int x = 0, y = 0;

    if (!pg_TwoIntsFromObj(args, &x, &y)) {
        return RAISE(PyExc_TypeError, "argument must contain two numbers");
    }

    self->r.x += x;
    self->r.y += y;
    Py_RETURN_NONE;
}

static PyObject *
pg_rect_inflate(pgRectObject *self, PyObject *args)
{
    int x = 0, y = 0;

    if (!pg_TwoIntsFromObj(args, &x, &y)) {
        return RAISE(PyExc_TypeError, "argument must contain two numbers");
    }

    return _pg_rect_subtype_new4(Py_TYPE(self), self->r.x - x / 2,
                                 self->r.y - y / 2, self->r.w + x,
                                 self->r.h + y);
}

static PyObject *
pg_rect_inflate_ip(pgRectObject *self, PyObject *args)
{
    int x = 0, y = 0;

    if (!pg_TwoIntsFromObj(args, &x, &y)) {
        return RAISE(PyExc_TypeError, "argument must contain two numbers");
    }
    self->r.x -= x / 2;
    self->r.y -= y / 2;
    self->r.w += x;
    self->r.h += y;
    Py_RETURN_NONE;
}

static PyObject *
pg_rect_scale_by_ip(pgRectObject *self, PyObject *args, PyObject *kwargs)
{
    float factor_x, factor_y = 0;

    static char *keywords[] = {"x", "y", NULL};

    if (kwargs) {
        PyObject *scale_by = PyDict_GetItemString(kwargs, "scale_by");
        float temp_x, temp_y = 0;

        if (scale_by) {
            if (PyDict_Size(kwargs) > 1) {
                return RAISE(PyExc_TypeError,
                             "The 'scale_by' keyword cannot be combined with "
                             "other arguments.");
            }
            if (!pg_TwoFloatsFromObj(scale_by, &temp_x, &temp_y)) {
                PyErr_SetString(PyExc_TypeError, "number pair expected");
                return 0;
            }
            PyDict_SetItemString(kwargs, "x", PyFloat_FromDouble(temp_x));
            PyDict_SetItemString(kwargs, "y", PyFloat_FromDouble(temp_y));
            PyDict_DelItemString(kwargs, "scale_by");
        }
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "f|f", keywords, &factor_x,
                                     &factor_y)) {
        PyErr_SetString(PyExc_TypeError, "Float values expected.");
        return NULL;
    }

    factor_x = factor_x < 0 ? -factor_x : factor_x;
    factor_y = factor_y < 0 ? -factor_y : factor_y;

    factor_y = (factor_y > 0) ? factor_y : factor_x;

    self->r.x =
        (int)(self->r.x + (self->r.w / 2) - (self->r.w * factor_x / 2));
    self->r.y =
        (int)(self->r.y + (self->r.h / 2) - (self->r.h * factor_y / 2));
    self->r.w = (int)(self->r.w * factor_x);
    self->r.h = (int)(self->r.h * factor_y);
    Py_RETURN_NONE;
}

static PyObject *
pg_rect_scale_by(pgRectObject *self, PyObject *args, PyObject *kwargs)
{
    pgRectObject *returnRect = (pgRectObject *)_pg_rect_subtype_new4(
        Py_TYPE(self), self->r.x, self->r.y, self->r.w, self->r.h);
    pg_rect_scale_by_ip(returnRect, args, kwargs);
    return (PyObject *)returnRect;
}

static PyObject *
pg_rect_update(pgRectObject *self, PyObject *args)
{
    SDL_Rect temp;
    SDL_Rect *argrect = pgRect_FromObject(args, &temp);

    if (argrect == NULL) {
        return RAISE(PyExc_TypeError, "Argument must be rect style object");
    }
    self->r.x = argrect->x;
    self->r.y = argrect->y;
    self->r.w = argrect->w;
    self->r.h = argrect->h;
    Py_RETURN_NONE;
}

static PyObject *
pg_rect_union(pgRectObject *self, PyObject *args)
{
    SDL_Rect *argrect, temp;
    int x, y, w, h;

    if (!(argrect = pgRect_FromObject(args, &temp))) {
        return RAISE(PyExc_TypeError, "Argument must be rect style object");
    }
    x = MIN(self->r.x, argrect->x);
    y = MIN(self->r.y, argrect->y);
    w = MAX(self->r.x + self->r.w, argrect->x + argrect->w) - x;
    h = MAX(self->r.y + self->r.h, argrect->y + argrect->h) - y;
    return _pg_rect_subtype_new4(Py_TYPE(self), x, y, w, h);
}

static PyObject *
pg_rect_union_ip(pgRectObject *self, PyObject *args)
{
    SDL_Rect *argrect, temp;
    int x, y, w, h;

    if (!(argrect = pgRect_FromObject(args, &temp)))
        return RAISE(PyExc_TypeError, "Argument must be rect style object");

    x = MIN(self->r.x, argrect->x);
    y = MIN(self->r.y, argrect->y);
    w = MAX(self->r.x + self->r.w, argrect->x + argrect->w) - x;
    h = MAX(self->r.y + self->r.h, argrect->y + argrect->h) - y;
    self->r.x = x;
    self->r.y = y;
    self->r.w = w;
    self->r.h = h;
    Py_RETURN_NONE;
}

static PyObject *
pg_rect_unionall(pgRectObject *self, PyObject *args, PyObject *kwargs)
{
    SDL_Rect *argrect, temp;
    Py_ssize_t loop, size;
    PyObject *list, *obj;
    int t, l, b, r;

    static char *keywords[] = {"rect", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", keywords, &list)) {
        return NULL;
    }
    if (!PySequence_Check(list)) {
        return RAISE(PyExc_TypeError,
                     "Argument must be a sequence of rectstyle objects.");
    }

    l = self->r.x;
    t = self->r.y;
    r = self->r.x + self->r.w;
    b = self->r.y + self->r.h;
    size = PySequence_Length(list); /*warning, size could be -1 on error?*/
    if (size < 1) {
        if (size < 0) {
            /*Error.*/
            return NULL;
        }
        /*Empty list: nothing to be done.*/
        return _pg_rect_subtype_new4(Py_TYPE(self), l, t, r - l, b - t);
    }

    for (loop = 0; loop < size; ++loop) {
        obj = PySequence_GetItem(list, loop);
        if (!obj || !(argrect = pgRect_FromObject(obj, &temp))) {
            Py_XDECREF(obj);
            return RAISE(PyExc_TypeError,
                         "Argument must be a sequence of rectstyle objects.");
        }
        l = MIN(l, argrect->x);
        t = MIN(t, argrect->y);
        r = MAX(r, argrect->x + argrect->w);
        b = MAX(b, argrect->y + argrect->h);
        Py_DECREF(obj);
    }
    return _pg_rect_subtype_new4(Py_TYPE(self), l, t, r - l, b - t);
}

static PyObject *
pg_rect_unionall_ip(pgRectObject *self, PyObject *args, PyObject *kwargs)
{
    SDL_Rect *argrect, temp;
    Py_ssize_t loop, size;
    PyObject *list, *obj;
    int t, l, b, r;

    static char *keywords[] = {"rects", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", keywords, &list)) {
        return NULL;
    }
    if (!PySequence_Check(list)) {
        return RAISE(PyExc_TypeError,
                     "Argument must be a sequence of rectstyle objects.");
    }

    l = self->r.x;
    t = self->r.y;
    r = self->r.x + self->r.w;
    b = self->r.y + self->r.h;

    size = PySequence_Length(list); /*warning, size could be -1 on error?*/
    if (size < 1) {
        if (size < 0) {
            /*Error.*/
            return NULL;
        }
        /*Empty list: nothing to be done.*/
        Py_RETURN_NONE;
    }

    for (loop = 0; loop < size; ++loop) {
        obj = PySequence_GetItem(list, loop);
        if (!obj || !(argrect = pgRect_FromObject(obj, &temp))) {
            Py_XDECREF(obj);
            return RAISE(PyExc_TypeError,
                         "Argument must be a sequence of rectstyle objects.");
        }
        l = MIN(l, argrect->x);
        t = MIN(t, argrect->y);
        r = MAX(r, argrect->x + argrect->w);
        b = MAX(b, argrect->y + argrect->h);
        Py_DECREF(obj);
    }

    self->r.x = l;
    self->r.y = t;
    self->r.w = r - l;
    self->r.h = b - t;
    Py_RETURN_NONE;
}

static PyObject *
pg_rect_collidepoint(pgRectObject *self, PyObject *args)
{
    int x = 0, y = 0;
    int inside;

    if (!pg_TwoIntsFromObj(args, &x, &y)) {
        return RAISE(PyExc_TypeError, "argument must contain two numbers");
    }

    inside = x >= self->r.x && x < self->r.x + self->r.w && y >= self->r.y &&
             y < self->r.y + self->r.h;

    return PyBool_FromLong(inside);
}

static PyObject *
pg_rect_colliderect(pgRectObject *self, PyObject *const *args,
                    Py_ssize_t nargs)
{
    /* This function got changed to use the FASTCALL calling convention in
     * Python 3.7. This lets us exploit the fact that we don't have an
     * intermediate Tuple args object to extract all the arguments from, saving
     * us performance in the process. This decoupling forces us to deal with
     * all the different cases (dictated by the number of parameters) by
     * building specific code paths.
     * Given that this function accepts any Rect-like object, there are 3 main
     * cases to deal with:
     * - 1 parameter: a Rect-like object
     * - 2 parameters: two sequences that represent the position and dimensions
     * of the Rect
     * - 4 parameters: four numbers that represent the position and dimensions
     */

    SDL_Rect srect = self->r;
    SDL_Rect temp;

    if (nargs == 1) {
        /* One argument was passed in, so we assume it's a rectstyle object.
         * This could mean several of the following (all dealt by
         * pgRect_FromObject):
         * - (x, y, w, h)
         * - ((x, y), (w, h))
         * - Rect
         * - Object with "rect" attribute
         */
        SDL_Rect *tmp;
        if (!(tmp = pgRect_FromObject(args[0], &temp))) {
            if (PyErr_Occurred()) {
                return NULL;
            }
            else {
                return RAISE(PyExc_TypeError,
                             "Invalid rect, all 4 fields must be numeric");
            }
        }
        return PyBool_FromLong(_pg_do_rects_intersect(&srect, tmp));
    }
    else if (nargs == 2) {
        /* Two separate sequences were passed in:
         * - (x, y), (w, h)
         */
        if (!pg_TwoIntsFromObj(args[0], &temp.x, &temp.y) ||
            !pg_TwoIntsFromObj(args[1], &temp.w, &temp.h)) {
            if (PyErr_Occurred())
                return NULL;
            else
                return RAISE(PyExc_TypeError,
                             "Invalid rect, all 4 fields must be numeric");
        }
    }
    else if (nargs == 4) {
        /* Four separate arguments were passed in:
         * - x, y, w, h
         */
        if (!(pg_IntFromObj(args[0], &temp.x))) {
            return RAISE(PyExc_TypeError,
                         "Invalid x value for rect, must be numeric");
        }

        if (!(pg_IntFromObj(args[1], &temp.y))) {
            return RAISE(PyExc_TypeError,
                         "Invalid y value for rect, must be numeric");
        }

        if (!(pg_IntFromObj(args[2], &temp.w))) {
            return RAISE(PyExc_TypeError,
                         "Invalid w value for rect, must be numeric");
        }

        if (!(pg_IntFromObj(args[3], &temp.h))) {
            return RAISE(PyExc_TypeError,
                         "Invalid h value for rect, must be numeric");
        }
    }
    else {
        return RAISE(PyExc_ValueError,
                     "Incorrect arguments number, must be either 1, 2 or 4");
    }

    return PyBool_FromLong(_pg_do_rects_intersect(&srect, &temp));
}
PG_WRAP_FASTCALL_FUNC(pg_rect_colliderect, pgRectObject)

static PyObject *
pg_rect_collidelist(pgRectObject *self, PyObject *args, PyObject *kwargs)
{
    SDL_Rect *argrect, temp;
    Py_ssize_t size;
    int loop;
    PyObject *list, *obj;
    PyObject *ret = NULL;

    static char *keywords[] = {"rects", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", keywords, &list)) {
        return NULL;
    }

    if (!PySequence_Check(list)) {
        return RAISE(PyExc_TypeError,
                     "Argument must be a sequence of rectstyle objects.");
    }

    size = PySequence_Length(list); /*warning, size could be -1 on error?*/
    for (loop = 0; loop < size; ++loop) {
        obj = PySequence_GetItem(list, loop);
        if (!obj || !(argrect = pgRect_FromObject(obj, &temp))) {
            PyErr_SetString(
                PyExc_TypeError,
                "Argument must be a sequence of rectstyle objects.");
            Py_XDECREF(obj);
            break;
        }
        if (_pg_do_rects_intersect(&self->r, argrect)) {
            ret = PyLong_FromLong(loop);
            Py_DECREF(obj);
            break;
        }
        Py_DECREF(obj);
    }
    if (loop == size) {
        ret = PyLong_FromLong(-1);
    }

    return ret;
}

static PyObject *
pg_rect_collidelistall(pgRectObject *self, PyObject *args, PyObject *kwargs)
{
    SDL_Rect *argrect, temp;
    Py_ssize_t size;
    int loop;
    PyObject *list, *obj;
    PyObject *ret = NULL;

    static char *keywords[] = {"rects", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", keywords, &list)) {
        return NULL;
    }

    if (!PySequence_Check(list)) {
        return RAISE(PyExc_TypeError,
                     "Argument must be a sequence of rectstyle objects.");
    }

    ret = PyList_New(0);
    if (!ret) {
        return NULL;
    }

    size = PySequence_Length(list); /*warning, size could be -1?*/
    for (loop = 0; loop < size; ++loop) {
        obj = PySequence_GetItem(list, loop);

        if (!obj || !(argrect = pgRect_FromObject(obj, &temp))) {
            Py_XDECREF(obj);
            Py_DECREF(ret);
            return RAISE(PyExc_TypeError,
                         "Argument must be a sequence of rectstyle objects.");
        }

        if (_pg_do_rects_intersect(&self->r, argrect)) {
            PyObject *num = PyLong_FromLong(loop);
            if (!num) {
                Py_DECREF(ret);
                Py_DECREF(obj);
                return NULL;
            }
            if (0 != PyList_Append(ret, num)) {
                Py_DECREF(ret);
                Py_DECREF(num);
                Py_DECREF(obj);
                return NULL; /* Exception already set. */
            }
            Py_DECREF(num);
        }
        Py_DECREF(obj);
    }

    return ret;
}

static SDL_Rect *
pgRect_FromObjectAndKeyFunc(PyObject *obj, PyObject *keyfunc, SDL_Rect *temp)
{
    if (keyfunc) {
        PyObject *obj_with_rect =
            PyObject_CallFunctionObjArgs(keyfunc, obj, NULL);
        if (!obj_with_rect) {
            return NULL;
        }

        SDL_Rect *ret = pgRect_FromObject(obj_with_rect, temp);
        Py_DECREF(obj_with_rect);
        if (!ret) {
            PyErr_SetString(
                PyExc_TypeError,
                "Key function must return rect or rect-like objects");
            return NULL;
        }
        return ret;
    }
    else {
        SDL_Rect *ret = pgRect_FromObject(obj, temp);
        if (!ret) {
            PyErr_SetString(PyExc_TypeError,
                            "Sequence must contain rect or rect-like objects");
            return NULL;
        }
        return ret;
    }
}

static PyObject *
pg_rect_collideobjectsall(pgRectObject *self, PyObject *args, PyObject *kwargs)
{
    SDL_Rect *argrect;
    SDL_Rect temp;
    Py_ssize_t size;
    int loop;
    PyObject *list, *obj;
    PyObject *keyfunc = NULL;
    PyObject *ret = NULL;
    static char *keywords[] = {"list", "key", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|$O:collideobjectsall",
                                     keywords, &list, &keyfunc)) {
        return NULL;
    }

    if (!PySequence_Check(list)) {
        return RAISE(PyExc_TypeError,
                     "Argument must be a sequence of objects.");
    }

    if (keyfunc == Py_None) {
        keyfunc = NULL;
    }

    if (keyfunc && !PyCallable_Check(keyfunc)) {
        return RAISE(PyExc_TypeError,
                     "Key function must be callable with one argument.");
    }

    ret = PyList_New(0);
    if (!ret) {
        return NULL;
    }

    size = PySequence_Length(list);
    if (size == -1) {
        Py_DECREF(ret);
        return NULL;
    }

    for (loop = 0; loop < size; ++loop) {
        obj = PySequence_GetItem(list, loop);

        if (!obj) {
            Py_DECREF(ret);
            return NULL;
        }

        if (!(argrect = pgRect_FromObjectAndKeyFunc(obj, keyfunc, &temp))) {
            Py_XDECREF(obj);
            Py_DECREF(ret);
            return NULL;
        }

        if (_pg_do_rects_intersect(&self->r, argrect)) {
            if (0 != PyList_Append(ret, obj)) {
                Py_DECREF(ret);
                Py_DECREF(obj);
                return NULL; /* Exception already set. */
            }
        }
        Py_DECREF(obj);
    }

    return ret;
}

static PyObject *
pg_rect_collideobjects(pgRectObject *self, PyObject *args, PyObject *kwargs)
{
    SDL_Rect *argrect;
    SDL_Rect temp;
    Py_ssize_t size;
    int loop;
    PyObject *list, *obj;
    PyObject *keyfunc = NULL;
    static char *keywords[] = {"list", "key", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|$O:collideobjects",
                                     keywords, &list, &keyfunc)) {
        return NULL;
    }

    if (!PySequence_Check(list)) {
        return RAISE(PyExc_TypeError,
                     "Argument must be a sequence of objects.");
    }

    if (keyfunc == Py_None) {
        keyfunc = NULL;
    }

    if (keyfunc && !PyCallable_Check(keyfunc)) {
        return RAISE(PyExc_TypeError,
                     "Key function must be callable with one argument.");
    }

    size = PySequence_Length(list);
    if (size == -1) {
        return NULL;
    }

    for (loop = 0; loop < size; ++loop) {
        obj = PySequence_GetItem(list, loop);

        if (!obj) {
            return NULL;
        }

        if (!(argrect = pgRect_FromObjectAndKeyFunc(obj, keyfunc, &temp))) {
            Py_XDECREF(obj);
            return NULL;
        }

        if (_pg_do_rects_intersect(&self->r, argrect)) {
            return obj;
        }
        Py_DECREF(obj);
    }

    Py_RETURN_NONE;
}

static PyObject *
pg_rect_collidedict(pgRectObject *self, PyObject *args, PyObject *kwargs)
{
    SDL_Rect *argrect, temp;
    Py_ssize_t loop = 0;
    Py_ssize_t values = 0; /* Defaults to expecting keys as rects. */
    PyObject *dict, *key, *val;
    PyObject *ret = NULL;

    static char *keywords[] = {"rect_dict", "values", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|i", keywords, &dict,
                                     &values)) {
        return NULL;
    }

    if (!PyDict_Check(dict)) {
        return RAISE(PyExc_TypeError, "first argument must be a dict");
    }

    while (PyDict_Next(dict, &loop, &key, &val)) {
        if (values) {
            if (!(argrect = pgRect_FromObject(val, &temp))) {
                return RAISE(PyExc_TypeError,
                             "dict must have rectstyle values");
            }
        }
        else {
            if (!(argrect = pgRect_FromObject(key, &temp))) {
                return RAISE(PyExc_TypeError, "dict must have rectstyle keys");
            }
        }

        if (_pg_do_rects_intersect(&self->r, argrect)) {
            ret = Py_BuildValue("(OO)", key, val);
            break;
        }
    }

    if (!ret) {
        Py_RETURN_NONE;
    }
    return ret;
}

static PyObject *
pg_rect_collidedictall(pgRectObject *self, PyObject *args, PyObject *kwargs)
{
    SDL_Rect *argrect, temp;
    Py_ssize_t loop = 0;
    Py_ssize_t values = 0; /* Defaults to expecting keys as rects. */
    PyObject *dict, *key, *val;
    PyObject *ret = NULL;

    static char *keywords[] = {"rect_dict", "values", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|i", keywords, &dict,
                                     &values)) {
        return NULL;
    }

    if (!PyDict_Check(dict)) {
        return RAISE(PyExc_TypeError, "first argument must be a dict");
    }

    ret = PyList_New(0);
    if (!ret)
        return NULL;

    while (PyDict_Next(dict, &loop, &key, &val)) {
        if (values) {
            if (!(argrect = pgRect_FromObject(val, &temp))) {
                Py_DECREF(ret);
                return RAISE(PyExc_TypeError,
                             "dict must have rectstyle values");
            }
        }
        else {
            if (!(argrect = pgRect_FromObject(key, &temp))) {
                Py_DECREF(ret);
                return RAISE(PyExc_TypeError, "dict must have rectstyle keys");
            }
        }

        if (_pg_do_rects_intersect(&self->r, argrect)) {
            PyObject *num = Py_BuildValue("(OO)", key, val);
            if (!num) {
                Py_DECREF(ret);
                return NULL;
            }
            if (0 != PyList_Append(ret, num)) {
                Py_DECREF(ret);
                Py_DECREF(num);
                return NULL; /* Exception already set. */
            }
            Py_DECREF(num);
        }
    }

    return ret;
}

static PyObject *
pg_rect_clip(pgRectObject *self, PyObject *args)
{
    SDL_Rect *A, *B, temp;
    int x, y, w, h;

    A = &self->r;
    if (!(B = pgRect_FromObject(args, &temp))) {
        return RAISE(PyExc_TypeError, "Argument must be rect style object");
    }

    /* Left */
    if ((A->x >= B->x) && (A->x < (B->x + B->w))) {
        x = A->x;
    }
    else if ((B->x >= A->x) && (B->x < (A->x + A->w)))
        x = B->x;
    else
        goto nointersect;

    /* Right */
    if (((A->x + A->w) > B->x) && ((A->x + A->w) <= (B->x + B->w))) {
        w = (A->x + A->w) - x;
    }
    else if (((B->x + B->w) > A->x) && ((B->x + B->w) <= (A->x + A->w)))
        w = (B->x + B->w) - x;
    else
        goto nointersect;

    /* Top */
    if ((A->y >= B->y) && (A->y < (B->y + B->h))) {
        y = A->y;
    }
    else if ((B->y >= A->y) && (B->y < (A->y + A->h)))
        y = B->y;
    else
        goto nointersect;

    /* Bottom */
    if (((A->y + A->h) > B->y) && ((A->y + A->h) <= (B->y + B->h))) {
        h = (A->y + A->h) - y;
    }
    else if (((B->y + B->h) > A->y) && ((B->y + B->h) <= (A->y + A->h)))
        h = (B->y + B->h) - y;
    else
        goto nointersect;

    return _pg_rect_subtype_new4(Py_TYPE(self), x, y, w, h);

nointersect:
    return _pg_rect_subtype_new4(Py_TYPE(self), A->x, A->y, 0, 0);
}

/* clipline() - crops the given line within the rect
 *
 * Supported argument formats:
 *     clipline(x1, y1, x2, y2) - 4 ints
 *     clipline((x1, y1), (x2, y2)) - 2 sequences of 2 ints
 *     clipline(((x1, y1), (x2, y2))) - 1 sequence of 2 sequences of 2 ints
 *     clipline((x1, y1, x2, y2)) - 1 sequence of 4 ints
 *
 * Returns:
 *     PyObject: containing one of the following tuples
 *         ((x1, y1), (x2, y2)) - tuple of 2 tuples of 2 ints, cropped input
 *                                line if line intersects with rect
 *         () - empty tuple, if no intersection
 */
static PyObject *
pg_rect_clipline(pgRectObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *arg1 = NULL, *arg2 = NULL, *arg3 = NULL, *arg4 = NULL;
    SDL_Rect *rect = &self->r, *rect_copy = NULL;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    int collision_detected = 0; // Flag to check if a collision was detected

    static char *keywords[] = {"x1", "x2", "x3", "x4", NULL};

    // Handle keyword arguments 'first_coordinate' and 'second_coordinate'
    if (kwargs) {
        int temp_x1 = 0, temp_x2 = 0, temp_x3 = 0, temp_x4 = 0;

        PyObject *first_coord =
            PyDict_GetItemString(kwargs, "first_coordinate");
        PyObject *second_coord =
            PyDict_GetItemString(kwargs, "second_coordinate");

        // If both 'first_coordinate' and 'second_coordinate' are provided
        if (first_coord && second_coord) {
            if (PyDict_Size(kwargs) > 2) {
                return RAISE(
                    PyExc_TypeError,
                    "Only 2 keyword arguments can be passed when using 'first_coordinate' and 'second_coordinate'");
            }

            if (!pg_TwoIntsFromObj(first_coord, &temp_x1, &temp_x2)) {
                PyErr_SetString(PyExc_TypeError,
                                "number pair expected for first argument");
                return 0;
            }

            PyDict_SetItemString(kwargs, "x1", PyLong_FromLong(temp_x1));
            PyDict_SetItemString(kwargs, "x2", PyLong_FromLong(temp_x2));
            PyDict_DelItemString(kwargs, "first_coordinate");

            if (!pg_TwoIntsFromObj(second_coord, &temp_x3, &temp_x4)) {
                PyErr_SetString(PyExc_TypeError,
                                "number pair expected for second argument");
                return 0;
            }

            PyDict_SetItemString(kwargs, "x3", PyLong_FromLong(temp_x3));
            PyDict_SetItemString(kwargs, "x4", PyLong_FromLong(temp_x4));
            PyDict_DelItemString(kwargs, "second_coordinate");
        }

        // Handles 'rect_arg' scenarios
        PyObject *rect_arg = PyDict_GetItemString(kwargs, "rect_arg");

        if (rect_arg) {
            if (PyDict_Size(kwargs) > 1) {
                return RAISE(PyExc_TypeError,
                             "Only 1 keyword argument can be passed when using 'rect_arg'");
            } else if (!four_ints_from_obj(rect_arg, &temp_x1, &temp_x2,
                                           &temp_x3, &temp_x4)) {
                return 0;  // Exception already set
            }
            PyDict_SetItemString(kwargs, "x1", PyLong_FromLong(temp_x1));
            PyDict_SetItemString(kwargs, "x2", PyLong_FromLong(temp_x2));
            PyDict_SetItemString(kwargs, "x3", PyLong_FromLong(temp_x3));
            PyDict_SetItemString(kwargs, "x4", PyLong_FromLong(temp_x4));
            PyDict_DelItemString(kwargs, "rect_arg");
        }
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|OOO", keywords, &arg1,
                                     &arg2, &arg3, &arg4)) {
        return NULL; // Exception already set
    }

    if (arg2 == NULL) {
        // Handle formats:
        //     clipline(((x1, y1), (x2, y2)))
        //     clipline((x1, y1, x2, y2))
        if (!four_ints_from_obj(arg1, &x1, &y1, &x2, &y2)) {
            return NULL; // Exception already set
        }
    } else if (arg3 == NULL) {
        // Handle format: clipline((x1, y1), (x2, y2))
        int result = pg_TwoIntsFromObj(arg1, &x1, &y1);

        if (!result) {
            return RAISE(PyExc_TypeError,
                         "Number pair expected for the first argument");
        }

        // Get the other end of the line
        result = pg_TwoIntsFromObj(arg2, &x2, &y2);

        if (!result) {
            return RAISE(PyExc_TypeError,
                         "Number pair expected for the second argument");
        }
    } else if (arg4 != NULL) {
        // Handle format: clipline(x1, y1, x2, y2)
        int result = pg_IntFromObj(arg1, &x1);

        if (!result) {
            return RAISE(PyExc_TypeError,
                         "Number expected for the first argument");
        }

        result = pg_IntFromObj(arg2, &y1);

        if (!result) {
            return RAISE(PyExc_TypeError,
                         "Number expected for the second argument");
        }

        result = pg_IntFromObj(arg3, &x2);

        if (!result) {
            return RAISE(PyExc_TypeError,
                         "Number expected for the third argument");
        }

        result = pg_IntFromObj(arg4, &y2);

        if (!result) {
            return RAISE(PyExc_TypeError,
                         "Number expected for the fourth argument");
        }
    } else {
        return RAISE(PyExc_TypeError,
                     "clipline() takes 1, 2, or 4 arguments (3 given)");
    }

    // Normalize the rectangle if necessary
    if ((self->r.w < 0) || (self->r.h < 0)) {
        // Make a copy of the rect so it can be normalized
        rect_copy = &pgRect_AsRect(pgRect_New(&self->r));

        if (NULL == rect_copy) {
            return RAISE(PyExc_MemoryError, "Cannot allocate memory for rect");
        }

        pgRect_Normalize(rect_copy);
        rect = rect_copy;
    }

    // Ensure multiple collision data is not overwritten
    if (SDL_IntersectRectAndLine(rect, &x1, &y1, &x2, &y2)) {
        collision_detected = 1; // Mark that a collision was detected
    }

    Py_XDECREF(rect_copy);

    if (collision_detected) {
        // Return the clipped line coordinates
        return Py_BuildValue("((ii)(ii))", x1, y1, x2, y2);
    }
    return PyTuple_New(0); // No collision detected
}

static int
_pg_rect_contains(pgRectObject *self, PyObject *arg)
{
    SDL_Rect *argrect, temp_arg;
    if (!(argrect = pgRect_FromObject((PyObject *)arg, &temp_arg))) {
        return -1;
    }
    return (self->r.x <= argrect->x) && (self->r.y <= argrect->y) &&
           (self->r.x + self->r.w >= argrect->x + argrect->w) &&
           (self->r.y + self->r.h >= argrect->y + argrect->h) &&
           (self->r.x + self->r.w > argrect->x) &&
           (self->r.y + self->r.h > argrect->y);
}

static PyObject *
pg_rect_contains(pgRectObject *self, PyObject *arg)
{
    int ret = _pg_rect_contains(self, arg);
    if (ret < 0) {
        return RAISE(PyExc_TypeError, "Argument must be rect style object");
    }
    return PyBool_FromLong(ret);
}

static int
pg_rect_contains_seq(pgRectObject *self, PyObject *arg)
{
    if (PyLong_Check(arg)) {
        int coord = (int)PyLong_AsLong(arg);
        return coord == self->r.x || coord == self->r.y ||
               coord == self->r.w || coord == self->r.h;
    }
    int ret = _pg_rect_contains(self, arg);
    if (ret < 0) {
        PyErr_SetString(PyExc_TypeError,
                        "'in <pygame.Rect>' requires rect style object"
                        " or int as left operand");
    }
    return ret;
}

static PyObject *
pg_rect_clamp(pgRectObject *self, PyObject *args)
{
    SDL_Rect *argrect, temp;
    int x, y;

    if (!(argrect = pgRect_FromObject(args, &temp))) {
        return RAISE(PyExc_TypeError, "Argument must be rect style object");
    }

    if (self->r.w >= argrect->w) {
        x = argrect->x + argrect->w / 2 - self->r.w / 2;
    }
    else if (self->r.x < argrect->x)
        x = argrect->x;
    else if (self->r.x + self->r.w > argrect->x + argrect->w)
        x = argrect->x + argrect->w - self->r.w;
    else
        x = self->r.x;

    if (self->r.h >= argrect->h) {
        y = argrect->y + argrect->h / 2 - self->r.h / 2;
    }
    else if (self->r.y < argrect->y)
        y = argrect->y;
    else if (self->r.y + self->r.h > argrect->y + argrect->h)
        y = argrect->y + argrect->h - self->r.h;
    else
        y = self->r.y;

    return _pg_rect_subtype_new4(Py_TYPE(self), x, y, self->r.w, self->r.h);
}

static PyObject *
pg_rect_fit(pgRectObject *self, PyObject *args)
{
    SDL_Rect *argrect, temp;
    int w, h, x, y;
    float xratio, yratio, maxratio;

    if (!(argrect = pgRect_FromObject(args, &temp))) {
        return RAISE(PyExc_TypeError, "Argument must be rect style object");
    }

    xratio = (float)self->r.w / (float)argrect->w;
    yratio = (float)self->r.h / (float)argrect->h;
    maxratio = (xratio > yratio) ? xratio : yratio;

    w = (int)(self->r.w / maxratio);
    h = (int)(self->r.h / maxratio);

    x = argrect->x + (argrect->w - w) / 2;
    y = argrect->y + (argrect->h - h) / 2;

    return _pg_rect_subtype_new4(Py_TYPE(self), x, y, w, h);
}

static PyObject *
pg_rect_clamp_ip(pgRectObject *self, PyObject *args)
{
    SDL_Rect *argrect, temp;
    int x, y;

    if (!(argrect = pgRect_FromObject(args, &temp))) {
        return RAISE(PyExc_TypeError, "Argument must be rect style object");
    }

    if (self->r.w >= argrect->w) {
        x = argrect->x + argrect->w / 2 - self->r.w / 2;
    }
    else if (self->r.x < argrect->x)
        x = argrect->x;
    else if (self->r.x + self->r.w > argrect->x + argrect->w)
        x = argrect->x + argrect->w - self->r.w;
    else
        x = self->r.x;

    if (self->r.h >= argrect->h) {
        y = argrect->y + argrect->h / 2 - self->r.h / 2;
    }
    else if (self->r.y < argrect->y)
        y = argrect->y;
    else if (self->r.y + self->r.h > argrect->y + argrect->h)
        y = argrect->y + argrect->h - self->r.h;
    else
        y = self->r.y;

    self->r.x = x;
    self->r.y = y;
    Py_RETURN_NONE;
}

/* for pickling */
static PyObject *
pg_rect_reduce(pgRectObject *self, PyObject *_null)
{
    return Py_BuildValue("(O(iiii))", Py_TYPE(self), (int)self->r.x,
                         (int)self->r.y, (int)self->r.w, (int)self->r.h);
}

/* for copy module */
static PyObject *
pg_rect_copy(pgRectObject *self, PyObject *_null)
{
    return _pg_rect_subtype_new4(Py_TYPE(self), self->r.x, self->r.y,
                                 self->r.w, self->r.h);
}

static struct PyMethodDef pg_rect_methods[] = {
    {"normalize", (PyCFunction)pg_rect_normalize, METH_NOARGS,
     DOC_RECTNORMALIZE},
    {"clip", (PyCFunction)pg_rect_clip, METH_VARARGS, DOC_RECTCLIP},
    {"clipline", (PyCFunction)pg_rect_clipline, METH_VARARGS | METH_KEYWORDS,
     DOC_RECTCLIPLINE},
    {"clamp", (PyCFunction)pg_rect_clamp, METH_VARARGS, DOC_RECTCLAMP},
    {"clamp_ip", (PyCFunction)pg_rect_clamp_ip, METH_VARARGS, DOC_RECTCLAMPIP},
    {"copy", (PyCFunction)pg_rect_copy, METH_NOARGS, DOC_RECTCOPY},
    {"fit", (PyCFunction)pg_rect_fit, METH_VARARGS, DOC_RECTFIT},
    {"move", (PyCFunction)pg_rect_move, METH_VARARGS, DOC_RECTMOVE},
    {"update", (PyCFunction)pg_rect_update, METH_VARARGS, DOC_RECTUPDATE},
    {"scale_by", (PyCFunction)pg_rect_scale_by, METH_VARARGS | METH_KEYWORDS,
     DOC_RECTSCALEBY},
    {"inflate", (PyCFunction)pg_rect_inflate, METH_VARARGS, DOC_RECTINFLATE},
    {"union", (PyCFunction)pg_rect_union, METH_VARARGS, DOC_RECTUNION},
    {"unionall", (PyCFunction)pg_rect_unionall, METH_VARARGS | METH_KEYWORDS,
     DOC_RECTUNIONALL},
    {"move_ip", (PyCFunction)pg_rect_move_ip, METH_VARARGS, DOC_RECTMOVEIP},
    {"inflate_ip", (PyCFunction)pg_rect_inflate_ip, METH_VARARGS,
     DOC_RECTINFLATEIP},
    {"scale_by_ip", (PyCFunction)pg_rect_scale_by_ip,
     METH_VARARGS | METH_KEYWORDS, DOC_RECTSCALEBYIP},
    {"union_ip", (PyCFunction)pg_rect_union_ip, METH_VARARGS, DOC_RECTUNIONIP},
    {"unionall_ip", (PyCFunction)pg_rect_unionall_ip,
     METH_VARARGS | METH_KEYWORDS, DOC_RECTUNIONALLIP},
    {"collidepoint", (PyCFunction)pg_rect_collidepoint, METH_VARARGS,
     DOC_RECTCOLLIDEPOINT},
    {"colliderect", (PyCFunction)PG_FASTCALL_NAME(pg_rect_colliderect),
     PG_FASTCALL, DOC_RECTCOLLIDERECT},
    {"collidelist", (PyCFunction)pg_rect_collidelist,
     METH_VARARGS | METH_KEYWORDS, DOC_RECTCOLLIDELIST},
    {"collidelistall", (PyCFunction)pg_rect_collidelistall,
     METH_VARARGS | METH_KEYWORDS, DOC_RECTCOLLIDELISTALL},
    {"collideobjectsall", (PyCFunction)pg_rect_collideobjectsall,
     METH_VARARGS | METH_KEYWORDS, DOC_RECTCOLLIDEOBJECTSALL},
    {"collideobjects", (PyCFunction)pg_rect_collideobjects,
     METH_VARARGS | METH_KEYWORDS, DOC_RECTCOLLIDEOBJECTS},
    {"collidedict", (PyCFunction)pg_rect_collidedict,
     METH_VARARGS | METH_KEYWORDS, DOC_RECTCOLLIDEDICT},
    {"collidedictall", (PyCFunction)pg_rect_collidedictall,
     METH_VARARGS | METH_KEYWORDS, DOC_RECTCOLLIDEDICTALL},
    {"contains", (PyCFunction)pg_rect_contains, METH_VARARGS,
     DOC_RECTCONTAINS},
    {"__reduce__", (PyCFunction)pg_rect_reduce, METH_NOARGS, NULL},
    {"__copy__", (PyCFunction)pg_rect_copy, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL}};

/* sequence functions */

static Py_ssize_t
pg_rect_length(PyObject *_self)
{
    return 4;
}

static PyObject *
pg_rect_item(pgRectObject *self, Py_ssize_t i)
{
    int *data = (int *)&self->r;

    if (i < 0 || i > 3) {
        if (i > -5 && i < 0) {
            i += 4;
        }
        else {
            return RAISE(PyExc_IndexError, "Invalid rect Index");
        }
    }
    return PyLong_FromLong(data[i]);
}

static int
pg_rect_ass_item(pgRectObject *self, Py_ssize_t i, PyObject *v)
{
    int val = 0;
    int *data = (int *)&self->r;

    if (!v) {
        PyErr_SetString(PyExc_TypeError, "item deletion is not supported");
        return -1;
    }

    if (i < 0 || i > 3) {
        if (i > -5 && i < 0) {
            i += 4;
        }
        else {
            PyErr_SetString(PyExc_IndexError, "Invalid rect Index");
            return -1;
        }
    }
    if (!pg_IntFromObj(v, &val)) {
        PyErr_SetString(PyExc_TypeError, "Must assign numeric values");
        return -1;
    }
    data[i] = val;
    return 0;
}

static PySequenceMethods pg_rect_as_sequence = {
    .sq_length = pg_rect_length,
    .sq_item = (ssizeargfunc)pg_rect_item,
    .sq_ass_item = (ssizeobjargproc)pg_rect_ass_item,
    .sq_contains = (objobjproc)pg_rect_contains_seq,
};

static PyObject *
pg_rect_subscript(pgRectObject *self, PyObject *op)
{
    int *data = (int *)&self->r;

    if (PyIndex_Check(op)) {
        PyObject *index = PyNumber_Index(op);
        Py_ssize_t i;

        if (index == NULL) {
            return NULL;
        }
        i = PyNumber_AsSsize_t(index, NULL);
        Py_DECREF(index);
        return pg_rect_item(self, i);
    }
    else if (op == Py_Ellipsis) {
        return Py_BuildValue("[iiii]", data[0], data[1], data[2], data[3]);
    }
    else if (PySlice_Check(op)) {
        PyObject *slice;
        Py_ssize_t start;
        Py_ssize_t stop;
        Py_ssize_t step;
        Py_ssize_t slicelen;
        Py_ssize_t i;
        PyObject *n;

        if (PySlice_GetIndicesEx(op, 4, &start, &stop, &step, &slicelen)) {
            return NULL;
        }

        slice = PyList_New(slicelen);
        if (slice == NULL) {
            return NULL;
        }
        for (i = 0; i < slicelen; ++i) {
            n = PyLong_FromSsize_t(data[start + (step * i)]);
            if (n == NULL) {
                Py_DECREF(slice);
                return NULL;
            }
            PyList_SET_ITEM(slice, i, n);
        }
        return slice;
    }

    return RAISE(PyExc_TypeError, "Invalid Rect slice");
}

static int
pg_rect_ass_subscript(pgRectObject *self, PyObject *op, PyObject *value)
{
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "item deletion is not supported");
        return -1;
    }

    if (PyIndex_Check(op)) {
        PyObject *index;
        Py_ssize_t i;

        index = PyNumber_Index(op);
        if (index == NULL) {
            return -1;
        }
        i = PyNumber_AsSsize_t(index, NULL);
        Py_DECREF(index);
        return pg_rect_ass_item(self, i, value);
    }
    else if (op == Py_Ellipsis) {
        int val = 0;

        if (pg_IntFromObj(value, &val)) {
            self->r.x = val;
            self->r.y = val;
            self->r.w = val;
            self->r.h = val;
        }
        else if (pgRect_Check(value)) {
            pgRectObject *rect = (pgRectObject *)value;

            self->r.x = rect->r.x;
            self->r.y = rect->r.y;
            self->r.w = rect->r.w;
            self->r.h = rect->r.h;
        }
        else if (PySequence_Check(value)) {
            PyObject *item;
            int values[4];
            Py_ssize_t i;

            if (PySequence_Size(value) != 4) {
                PyErr_SetString(PyExc_TypeError, "Expect a length 4 sequence");
                return -1;
            }
            for (i = 0; i < 4; ++i) {
                item = PySequence_ITEM(value, i);
                if (!pg_IntFromObj(item, values + i)) {
                    PyErr_Format(PyExc_TypeError,
                                 "Expected an integer between %d and %d",
                                 INT_MIN, INT_MAX);
                }
            }
            self->r.x = values[0];
            self->r.y = values[1];
            self->r.w = values[2];
            self->r.h = values[3];
        }
        else {
            PyErr_SetString(PyExc_TypeError,
                            "Expected an integer or sequence");
            return -1;
        }
    }
    else if (PySlice_Check(op)) {
        int *data = (int *)&self->r;
        Py_ssize_t start;
        Py_ssize_t stop;
        Py_ssize_t step;
        Py_ssize_t slicelen;
        int val = 0;
        Py_ssize_t i;

        if (PySlice_GetIndicesEx(op, 4, &start, &stop, &step, &slicelen)) {
            return -1;
        }

        if (pg_IntFromObj(value, &val)) {
            for (i = 0; i < slicelen; ++i) {
                data[start + step * i] = val;
            }
        }
        else if (PySequence_Check(value)) {
            PyObject *item;
            int values[4];
            Py_ssize_t size = PySequence_Size(value);

            if (size != slicelen) {
                PyErr_Format(PyExc_TypeError, "Expected a length %zd sequence",
                             slicelen);
                return -1;
            }
            for (i = 0; i < slicelen; ++i) {
                item = PySequence_ITEM(value, i);
                if (!pg_IntFromObj(item, values + i)) {
                    PyErr_Format(PyExc_TypeError,
                                 "Expected an integer between %d and %d",
                                 INT_MIN, INT_MAX);
                }
            }
            for (i = 0; i < slicelen; ++i) {
                data[start + step * i] = values[i];
            }
        }
        else {
            PyErr_SetString(PyExc_TypeError,
                            "Expected an integer or sequence");
            return -1;
        }
    }
    else {
        PyErr_SetString(PyExc_TypeError, "Invalid Rect slice");
        return -1;
    }
    return 0;
}

static PyMappingMethods pg_rect_as_mapping = {
    .mp_length = (lenfunc)pg_rect_length,
    .mp_subscript = (binaryfunc)pg_rect_subscript,
    .mp_ass_subscript = (objobjargproc)pg_rect_ass_subscript,
};

/* numeric functions */
static int
pg_rect_bool(pgRectObject *self)
{
    return self->r.w != 0 && self->r.h != 0;
}

static PyNumberMethods pg_rect_as_number = {
    .nb_bool = (inquiry)pg_rect_bool,
};

static PyObject *
pg_rect_repr(pgRectObject *self)
{
    return PyUnicode_FromFormat("<rect(%d, %d, %d, %d)>", self->r.x, self->r.y,
                                self->r.w, self->r.h);
}

static PyObject *
pg_rect_str(pgRectObject *self)
{
    return pg_rect_repr(self);
}

static PyObject *
pg_rect_richcompare(PyObject *o1, PyObject *o2, int opid)
{
    SDL_Rect *o1rect, *o2rect, temp1, temp2;
    int cmp;

    o1rect = pgRect_FromObject(o1, &temp1);
    if (!o1rect) {
        goto Unimplemented;
    }
    o2rect = pgRect_FromObject(o2, &temp2);
    if (!o2rect) {
        goto Unimplemented;
    }

    if (o1rect->x != o2rect->x) {
        cmp = o1rect->x < o2rect->x ? -1 : 1;
    }
    else if (o1rect->y != o2rect->y) {
        cmp = o1rect->y < o2rect->y ? -1 : 1;
    }
    else if (o1rect->w != o2rect->w) {
        cmp = o1rect->w < o2rect->w ? -1 : 1;
    }
    else if (o1rect->h != o2rect->h) {
        cmp = o1rect->h < o2rect->h ? -1 : 1;
    }
    else {
        cmp = 0;
    }

    switch (opid) {
        case Py_LT:
            return PyBool_FromLong(cmp < 0);
        case Py_LE:
            return PyBool_FromLong(cmp <= 0);
        case Py_EQ:
            return PyBool_FromLong(cmp == 0);
        case Py_NE:
            return PyBool_FromLong(cmp != 0);
        case Py_GT:
            return PyBool_FromLong(cmp > 0);
        case Py_GE:
            return PyBool_FromLong(cmp >= 0);
        default:
            break;
    }

Unimplemented:
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

static PyObject *
pg_rect_iterator(pgRectObject *self)
{
    Py_ssize_t i;
    int *data = (int *)&self->r;
    PyObject *iter, *tup = PyTuple_New(4);
    if (!tup) {
        return NULL;
    }
    for (i = 0; i < 4; i++) {
        PyObject *val = PyLong_FromLong(data[i]);
        if (!val) {
            Py_DECREF(tup);
            return NULL;
        }

        PyTuple_SET_ITEM(tup, i, val);
    }
    iter = PyTuple_Type.tp_iter(tup);
    Py_DECREF(tup);
    return iter;
}

static int
_rounded_int_from_object(PyObject *value, int *val)
{
    double tmpVal;

    if (!pg_DoubleFromObj(value, &tmpVal)) {
        PyErr_SetString(PyExc_TypeError, "invalid rect assignment");
        return 0;
    }

    if (tmpVal > INT_MAX || tmpVal < INT_MIN) {
        PyErr_Format(
            PyExc_TypeError,
            "invalid rect assignment, expected value between %d < x < %d",
            INT_MIN, INT_MAX);
        return 0;
    }

    *val = (int)pg_round(tmpVal);
    return 1;
}

static int
_rounded_two_ints_from_object(PyObject *value, int *val1, int *val2)
{
    double tmpVal1, tmpVal2;

    if (!pg_TwoDoublesFromObj(value, &tmpVal1, &tmpVal2)) {
        PyErr_SetString(PyExc_TypeError, "invalid rect assignment");
        return 0;
    }

    if (tmpVal1 > INT_MAX || tmpVal1 < INT_MIN || tmpVal2 > INT_MAX ||
        tmpVal2 < INT_MIN) {
        PyErr_Format(
            PyExc_TypeError,
            "invalid rect assignment, expected value between %d < x < %d",
            INT_MIN, INT_MAX);
        return 0;
    }

    *val1 = (int)pg_round(tmpVal1);
    *val2 = (int)pg_round(tmpVal2);
    return 1;
}

/*width*/
static PyObject *
pg_rect_getwidth(pgRectObject *self, void *closure)
{
    return PyLong_FromLong(self->r.w);
}

static int
pg_rect_setwidth(pgRectObject *self, PyObject *value, void *closure)
{
    int val1;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_int_from_object(value, &val1)) {
        return -1;
    }
    self->r.w = val1;
    return 0;
}

/*height*/
static PyObject *
pg_rect_getheight(pgRectObject *self, void *closure)
{
    return PyLong_FromLong(self->r.h);
}

static int
pg_rect_setheight(pgRectObject *self, PyObject *value, void *closure)
{
    int val1;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_int_from_object(value, &val1)) {
        return -1;
    }
    self->r.h = val1;
    return 0;
}

/*top*/
static PyObject *
pg_rect_gettop(pgRectObject *self, void *closure)
{
    return PyLong_FromLong(self->r.y);
}

static int
pg_rect_settop(pgRectObject *self, PyObject *value, void *closure)
{
    int val1;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_int_from_object(value, &val1)) {
        return -1;
    }
    self->r.y = val1;
    return 0;
}

/*left*/
static PyObject *
pg_rect_getleft(pgRectObject *self, void *closure)
{
    return PyLong_FromLong(self->r.x);
}

static int
pg_rect_setleft(pgRectObject *self, PyObject *value, void *closure)
{
    int val1;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_int_from_object(value, &val1)) {
        return -1;
    }
    self->r.x = val1;
    return 0;
}

/*right*/
static PyObject *
pg_rect_getright(pgRectObject *self, void *closure)
{
    return PyLong_FromLong(self->r.x + self->r.w);
}

static int
pg_rect_setright(pgRectObject *self, PyObject *value, void *closure)
{
    int val1;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_int_from_object(value, &val1)) {
        return -1;
    }
    self->r.x = val1 - self->r.w;
    return 0;
}

/*bottom*/
static PyObject *
pg_rect_getbottom(pgRectObject *self, void *closure)
{
    return PyLong_FromLong(self->r.y + self->r.h);
}

static int
pg_rect_setbottom(pgRectObject *self, PyObject *value, void *closure)
{
    int val1;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_int_from_object(value, &val1)) {
        return -1;
    }
    self->r.y = val1 - self->r.h;
    return 0;
}

/*centerx*/
static PyObject *
pg_rect_getcenterx(pgRectObject *self, void *closure)
{
    return PyLong_FromLong(self->r.x + (self->r.w >> 1));
}

static int
pg_rect_setcenterx(pgRectObject *self, PyObject *value, void *closure)
{
    int val1;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_int_from_object(value, &val1)) {
        return -1;
    }
    self->r.x = val1 - (self->r.w >> 1);
    return 0;
}

/*centery*/
static PyObject *
pg_rect_getcentery(pgRectObject *self, void *closure)
{
    return PyLong_FromLong(self->r.y + (self->r.h >> 1));
}

static int
pg_rect_setcentery(pgRectObject *self, PyObject *value, void *closure)
{
    int val1;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_int_from_object(value, &val1)) {
        return -1;
    }
    self->r.y = val1 - (self->r.h >> 1);
    return 0;
}

/*topleft*/
static PyObject *
pg_rect_gettopleft(pgRectObject *self, void *closure)
{
    return pg_tuple_couple_from_values_int(self->r.x, self->r.y);
}

static int
pg_rect_settopleft(pgRectObject *self, PyObject *value, void *closure)
{
    int val1, val2;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_two_ints_from_object(value, &val1, &val2)) {
        return -1;
    }
    self->r.x = val1;
    self->r.y = val2;
    return 0;
}

/*topright*/
static PyObject *
pg_rect_gettopright(pgRectObject *self, void *closure)
{
    return pg_tuple_couple_from_values_int(self->r.x + self->r.w, self->r.y);
}

static int
pg_rect_settopright(pgRectObject *self, PyObject *value, void *closure)
{
    int val1, val2;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_two_ints_from_object(value, &val1, &val2)) {
        return -1;
    }
    self->r.x = val1 - self->r.w;
    self->r.y = val2;
    return 0;
}

/*bottomleft*/
static PyObject *
pg_rect_getbottomleft(pgRectObject *self, void *closure)
{
    return pg_tuple_couple_from_values_int(self->r.x, self->r.y + self->r.h);
}

static int
pg_rect_setbottomleft(pgRectObject *self, PyObject *value, void *closure)
{
    int val1, val2;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_two_ints_from_object(value, &val1, &val2)) {
        return -1;
    }
    self->r.x = val1;
    self->r.y = val2 - self->r.h;
    return 0;
}

/*bottomright*/
static PyObject *
pg_rect_getbottomright(pgRectObject *self, void *closure)
{
    return pg_tuple_couple_from_values_int(self->r.x + self->r.w,
                                           self->r.y + self->r.h);
}

static int
pg_rect_setbottomright(pgRectObject *self, PyObject *value, void *closure)
{
    int val1, val2;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_two_ints_from_object(value, &val1, &val2)) {
        return -1;
    }
    self->r.x = val1 - self->r.w;
    self->r.y = val2 - self->r.h;
    return 0;
}

/*midtop*/
static PyObject *
pg_rect_getmidtop(pgRectObject *self, void *closure)
{
    return pg_tuple_couple_from_values_int(self->r.x + (self->r.w >> 1),
                                           self->r.y);
}

static int
pg_rect_setmidtop(pgRectObject *self, PyObject *value, void *closure)
{
    int val1, val2;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_two_ints_from_object(value, &val1, &val2)) {
        return -1;
    }
    self->r.x += val1 - (self->r.x + (self->r.w >> 1));
    self->r.y = val2;
    return 0;
}

/*midleft*/
static PyObject *
pg_rect_getmidleft(pgRectObject *self, void *closure)
{
    return pg_tuple_couple_from_values_int(self->r.x,
                                           self->r.y + (self->r.h >> 1));
}

static int
pg_rect_setmidleft(pgRectObject *self, PyObject *value, void *closure)
{
    int val1, val2;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_two_ints_from_object(value, &val1, &val2)) {
        return -1;
    }
    self->r.x = val1;
    self->r.y += val2 - (self->r.y + (self->r.h >> 1));
    return 0;
}

/*midbottom*/
static PyObject *
pg_rect_getmidbottom(pgRectObject *self, void *closure)
{
    return pg_tuple_couple_from_values_int(self->r.x + (self->r.w >> 1),
                                           self->r.y + self->r.h);
}

static int
pg_rect_setmidbottom(pgRectObject *self, PyObject *value, void *closure)
{
    int val1, val2;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_two_ints_from_object(value, &val1, &val2)) {
        return -1;
    }
    self->r.x += val1 - (self->r.x + (self->r.w >> 1));
    self->r.y = val2 - self->r.h;
    return 0;
}

/*midright*/
static PyObject *
pg_rect_getmidright(pgRectObject *self, void *closure)
{
    return pg_tuple_couple_from_values_int(self->r.x + self->r.w,
                                           self->r.y + (self->r.h >> 1));
}

static int
pg_rect_setmidright(pgRectObject *self, PyObject *value, void *closure)
{
    int val1, val2;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_two_ints_from_object(value, &val1, &val2)) {
        return -1;
    }
    self->r.x = val1 - self->r.w;
    self->r.y += val2 - (self->r.y + (self->r.h >> 1));
    return 0;
}

/*center*/
static PyObject *
pg_rect_getcenter(pgRectObject *self, void *closure)
{
    return pg_tuple_couple_from_values_int(self->r.x + (self->r.w >> 1),
                                           self->r.y + (self->r.h >> 1));
}

static int
pg_rect_setcenter(pgRectObject *self, PyObject *value, void *closure)
{
    int val1, val2;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_two_ints_from_object(value, &val1, &val2)) {
        return -1;
    }
    self->r.x += val1 - (self->r.x + (self->r.w >> 1));
    self->r.y += val2 - (self->r.y + (self->r.h >> 1));
    return 0;
}

/*size*/
static PyObject *
pg_rect_getsize(pgRectObject *self, void *closure)
{
    return pg_tuple_couple_from_values_int(self->r.w, self->r.h);
}

static int
pg_rect_setsize(pgRectObject *self, PyObject *value, void *closure)
{
    int val1, val2;

    if (NULL == value) {
        /* Attribute deletion not supported. */
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }

    if (!_rounded_two_ints_from_object(value, &val1, &val2)) {
        return -1;
    }
    self->r.w = val1;
    self->r.h = val2;
    return 0;
}

static PyObject *
pg_rect_getsafepickle(pgRectObject *self, void *closure)
{
    Py_RETURN_TRUE;
}

static PyGetSetDef pg_rect_getsets[] = {
    {"x", (getter)pg_rect_getleft, (setter)pg_rect_setleft, NULL, NULL},
    {"y", (getter)pg_rect_gettop, (setter)pg_rect_settop, NULL, NULL},
    {"w", (getter)pg_rect_getwidth, (setter)pg_rect_setwidth, NULL, NULL},
    {"h", (getter)pg_rect_getheight, (setter)pg_rect_setheight, NULL, NULL},
    {"width", (getter)pg_rect_getwidth, (setter)pg_rect_setwidth, NULL, NULL},
    {"height", (getter)pg_rect_getheight, (setter)pg_rect_setheight, NULL,
     NULL},
    {"top", (getter)pg_rect_gettop, (setter)pg_rect_settop, NULL, NULL},
    {"left", (getter)pg_rect_getleft, (setter)pg_rect_setleft, NULL, NULL},
    {"bottom", (getter)pg_rect_getbottom, (setter)pg_rect_setbottom, NULL,
     NULL},
    {"right", (getter)pg_rect_getright, (setter)pg_rect_setright, NULL, NULL},
    {"centerx", (getter)pg_rect_getcenterx, (setter)pg_rect_setcenterx, NULL,
     NULL},
    {"centery", (getter)pg_rect_getcentery, (setter)pg_rect_setcentery, NULL,
     NULL},
    {"topleft", (getter)pg_rect_gettopleft, (setter)pg_rect_settopleft, NULL,
     NULL},
    {"topright", (getter)pg_rect_gettopright, (setter)pg_rect_settopright,
     NULL, NULL},
    {"bottomleft", (getter)pg_rect_getbottomleft,
     (setter)pg_rect_setbottomleft, NULL, NULL},
    {"bottomright", (getter)pg_rect_getbottomright,
     (setter)pg_rect_setbottomright, NULL, NULL},
    {"midtop", (getter)pg_rect_getmidtop, (setter)pg_rect_setmidtop, NULL,
     NULL},
    {"midleft", (getter)pg_rect_getmidleft, (setter)pg_rect_setmidleft, NULL,
     NULL},
    {"midbottom", (getter)pg_rect_getmidbottom, (setter)pg_rect_setmidbottom,
     NULL, NULL},
    {"midright", (getter)pg_rect_getmidright, (setter)pg_rect_setmidright,
     NULL, NULL},
    {"size", (getter)pg_rect_getsize, (setter)pg_rect_setsize, NULL, NULL},
    {"center", (getter)pg_rect_getcenter, (setter)pg_rect_setcenter, NULL,
     NULL},

    {"__safe_for_unpickling__", (getter)pg_rect_getsafepickle, NULL, NULL,
     NULL},
    {NULL, 0, NULL, NULL, NULL} /* Sentinel */
};

static PyTypeObject pgRect_Type = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "pygame.rect.Rect",
    .tp_basicsize = sizeof(pgRectObject),
    .tp_dealloc = (destructor)pg_rect_dealloc,
    .tp_repr = (reprfunc)pg_rect_repr,
    .tp_as_number = &pg_rect_as_number,
    .tp_as_sequence = &pg_rect_as_sequence,
    .tp_as_mapping = &pg_rect_as_mapping,
    .tp_str = (reprfunc)pg_rect_str,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = DOC_PYGAMERECT,
    .tp_richcompare = (richcmpfunc)pg_rect_richcompare,
    .tp_weaklistoffset = offsetof(pgRectObject, weakreflist),
    .tp_iter = (getiterfunc)pg_rect_iterator,
    .tp_methods = pg_rect_methods,
    .tp_getset = pg_rect_getsets,
    .tp_init = (initproc)pg_rect_init,
    .tp_new = pg_rect_new,
};

static int
pg_rect_init(pgRectObject *self, PyObject *args, PyObject *kwds)
{
    SDL_Rect temp;
    SDL_Rect *argrect = pgRect_FromObject(args, &temp);

    if (argrect == NULL) {
        PyErr_SetString(PyExc_TypeError, "Argument must be rect style object");
        return -1;
    }
    self->r.x = argrect->x;
    self->r.y = argrect->y;
    self->r.w = argrect->w;
    self->r.h = argrect->h;
    return 0;
}

static PyMethodDef _pg_module_methods[] = {{NULL, NULL, 0, NULL}};

/*DOC*/ static char _pg_module_doc[] =
    /*DOC*/ "Module for the rectangle object\n";

MODINIT_DEFINE(rect)
{
    PyObject *module, *apiobj;
    static void *c_api[PYGAMEAPI_RECT_NUMSLOTS];

    static struct PyModuleDef _module = {PyModuleDef_HEAD_INIT,
                                         "rect",
                                         _pg_module_doc,
                                         -1,
                                         _pg_module_methods,
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL};

    /* import needed apis; Do this first so if there is an error
       the module is not loaded.
    */
    import_pygame_base();
    if (PyErr_Occurred()) {
        return NULL;
    }

    /* Create the module and add the functions */
    if (PyType_Ready(&pgRect_Type) < 0) {
        return NULL;
    }

    module = PyModule_Create(&_module);
    if (module == NULL) {
        return NULL;
    }

    Py_INCREF(&pgRect_Type);
    if (PyModule_AddObject(module, "RectType", (PyObject *)&pgRect_Type)) {
        Py_DECREF(&pgRect_Type);
        Py_DECREF(module);
        return NULL;
    }
    Py_INCREF(&pgRect_Type);
    if (PyModule_AddObject(module, "Rect", (PyObject *)&pgRect_Type)) {
        Py_DECREF(&pgRect_Type);
        Py_DECREF(module);
        return NULL;
    }

    /* export the c api */
    c_api[0] = &pgRect_Type;
    c_api[1] = pgRect_New;
    c_api[2] = pgRect_New4;
    c_api[3] = pgRect_FromObject;
    c_api[4] = pgRect_Normalize;
    apiobj = encapsulate_api(c_api, "rect");
    if (PyModule_AddObject(module, PYGAMEAPI_LOCAL_ENTRY, apiobj)) {
        Py_XDECREF(apiobj);
        Py_DECREF(module);
        return NULL;
    }
    return module;
}
