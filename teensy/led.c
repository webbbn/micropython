#include <stdio.h>

#include "misc.h"
#include "mpconfig.h"
#include "qstr.h"
#include "obj.h"
#include "led.h"

#include "Arduino.h"

void led_init(void) {
}

void led_state(pyb_led_t led, int state) {
    uint8_t pin;

    if (led == 0) {
        pin = LED_BUILTIN;
    } else {
        return;
    }
    digitalWrite(pin, state);
}

void led_toggle(pyb_led_t led) {
    uint8_t pin;

    if (led == 0) {
        pin = LED_BUILTIN;
    } else {
        return;
    }

    digitalWrite(pin, !digitalRead(pin));
}

/******************************************************************************/
/* Micro Python bindings                                                      */

typedef struct _pyb_led_obj_t {
    mp_obj_base_t base;
    uint led_id;
} pyb_led_obj_t;

void led_obj_print(void (*print)(void *env, const char *fmt, ...), void *env, mp_obj_t self_in, mp_print_kind_t kind) {
    pyb_led_obj_t *self = self_in;
    (void)kind;
    print(env, "<LED %lu>", self->led_id);
}

mp_obj_t led_obj_on(mp_obj_t self_in) {
    pyb_led_obj_t *self = self_in;
    led_state(self->led_id, 1);
    return mp_const_none;
}

mp_obj_t led_obj_off(mp_obj_t self_in) {
    pyb_led_obj_t *self = self_in;
    led_state(self->led_id, 0);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_1(led_obj_on_obj, led_obj_on);
static MP_DEFINE_CONST_FUN_OBJ_1(led_obj_off_obj, led_obj_off);

static const mp_method_t led_methods[] = {
    { "on", &led_obj_on_obj },
    { "off", &led_obj_off_obj },
    { NULL, NULL },
};

static const mp_obj_type_t led_obj_type = {
    { &mp_type_type },
    .name = MP_QSTR_Led,
    .print = led_obj_print,
    .methods = led_methods,
};

mp_obj_t pyb_Led(mp_obj_t led_id) {
    pyb_led_obj_t *o = m_new_obj(pyb_led_obj_t);
    o->base.type = &led_obj_type;
    o->led_id = mp_obj_get_int(led_id);
    return o;
}
