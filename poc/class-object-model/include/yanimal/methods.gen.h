/* GENERATED — do not edit. */
#ifndef POC_YANIMAL_METHODS_GEN_H
#define POC_YANIMAL_METHODS_GEN_H

#include "class.h"

struct str_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;

struct yetty_ycore_void_result yanimal_animal_ctor(struct object *obj);
typedef struct yetty_ycore_void_result (*yanimal_animal_ctor_fn)(struct object *);
struct yetty_ycore_void_result yanimal_animal_dtor(struct object *obj);
typedef struct yetty_ycore_void_result (*yanimal_animal_dtor_fn)(struct object *);
struct yetty_ycore_void_result yanimal_animal_breathe(struct object *obj);
typedef struct yetty_ycore_void_result (*yanimal_animal_breathe_fn)(struct object *);
struct str_result yanimal_animal_speak(struct object *obj, int volume);
typedef struct str_result (*yanimal_animal_speak_fn)(struct object *, int);
struct yetty_ycore_int_result yanimal_animal_eat(struct object *obj, float amount);
typedef struct yetty_ycore_int_result (*yanimal_animal_eat_fn)(struct object *, float);

#endif
