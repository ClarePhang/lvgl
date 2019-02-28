/**
 * @file lv_indev_proc.c
 *
 */

/*********************
 *      INCLUDES
 ********************/
#include "lv_indev.h"
#include "lv_disp.h"
#include "lv_obj.h"

#include "../lv_hal/lv_hal_tick.h"
#include "../lv_core/lv_group.h"
#include "../lv_core/lv_refr.h"
#include "../lv_misc/lv_task.h"
#include "../lv_misc/lv_math.h"

/*********************
 *      DEFINES
 *********************/

#if LV_INDEV_DRAG_THROW <= 0
#warning "LV_INDEV_DRAG_THROW must be greater than 0"
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

#if LV_INDEV_READ_PERIOD != 0
static void indev_proc_task(void * param);
static void indev_pointer_proc(lv_indev_t * i, lv_indev_data_t * data);
static void indev_keypad_proc(lv_indev_t * i, lv_indev_data_t * data);
static void indev_encoder_proc(lv_indev_t * i, lv_indev_data_t * data);
static void indev_button_proc(lv_indev_t * i, lv_indev_data_t * data);
static void indev_proc_press(lv_indev_proc_t * proc);
static void indev_proc_release(lv_indev_proc_t * proc);
static void indev_proc_reset_query_handler(lv_indev_t * indev);
static lv_obj_t * indev_search_obj(const lv_indev_proc_t * proc, lv_obj_t * obj);
static void indev_drag(lv_indev_proc_t * state);
static void indev_drag_throw(lv_indev_proc_t * proc);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_indev_t * indev_act;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Initialize the display input device subsystem
 */
void lv_indev_init(void)
{
#if LV_INDEV_READ_PERIOD != 0
    lv_task_create(indev_proc_task, LV_INDEV_READ_PERIOD, LV_TASK_PRIO_MID, NULL);
#endif

    lv_indev_reset(NULL);   /*Reset all input devices*/
}

/**
 * Get the currently processed input device. Can be used in action functions too.
 * @return pointer to the currently processed input device or NULL if no input device processing right now
 */
lv_indev_t * lv_indev_get_act(void)
{
    return indev_act;
}

/**
 * Get the type of an input device
 * @param indev pointer to an input device
 * @return the type of the input device from `lv_hal_indev_type_t` (`LV_INDEV_TYPE_...`)
 */
lv_hal_indev_type_t lv_indev_get_type(const lv_indev_t * indev)
{
    if(indev == NULL) return LV_INDEV_TYPE_NONE;

    return indev->driver.type;
}
/**
 * Reset one or all input devices
 * @param indev pointer to an input device to reset or NULL to reset all of them
 */
void lv_indev_reset(lv_indev_t * indev)
{
    if(indev) indev->proc.reset_query = 1;
    else {
        lv_indev_t * i = lv_indev_next(NULL);
        while(i) {
            i->proc.reset_query = 1;
            i = lv_indev_next(i);
        }
    }
}

/**
 * Reset the long press state of an input device
 * @param indev pointer to an input device
 */
void lv_indev_reset_lpr(lv_indev_t * indev)
{
    indev->proc.long_pr_sent = 0;
    indev->proc.longpr_rep_timestamp = lv_tick_get();
    indev->proc.pr_timestamp = lv_tick_get();
}

/**
 * Enable input devices device by type
 * @param type Input device type
 * @param enable true: enable this type; false: disable this type
 */
void lv_indev_enable(lv_hal_indev_type_t type, bool enable)
{

    lv_indev_t * i = lv_indev_next(NULL);

    while(i) {
        if(i->driver.type == type) i->proc.disabled = enable == false ? 1 : 0;
        i = lv_indev_next(i);
    }
}

/**
 * Set a cursor for a pointer input device (for LV_INPUT_TYPE_POINTER and LV_INPUT_TYPE_BUTTON)
 * @param indev pointer to an input device
 * @param cur_obj pointer to an object to be used as cursor
 */
void lv_indev_set_cursor(lv_indev_t * indev, lv_obj_t * cur_obj)
{
    if(indev->driver.type != LV_INDEV_TYPE_POINTER) return;

    indev->custom_data.cursor = cur_obj;
    lv_obj_set_parent(indev->custom_data.cursor, lv_disp_get_layer_sys(indev->driver.disp));
    lv_obj_set_pos(indev->custom_data.cursor, indev->proc.types.pointer.act_point.x,  indev->proc.types.pointer.act_point.y);
}

#if USE_LV_GROUP
/**
 * Set a destination group for a keypad input device (for LV_INDEV_TYPE_KEYPAD)
 * @param indev pointer to an input device
 * @param group point to a group
 */
void lv_indev_set_group(lv_indev_t * indev, lv_group_t * group)
{
    if(indev->driver.type == LV_INDEV_TYPE_KEYPAD || indev->driver.type == LV_INDEV_TYPE_ENCODER) {
        indev->custom_data.group = group;
    }
}
#endif

/**
 * Set the an array of points for LV_INDEV_TYPE_BUTTON.
 * These points will be assigned to the buttons to press a specific point on the screen
 * @param indev pointer to an input device
 * @param group point to a group
 */
void lv_indev_set_button_points(lv_indev_t * indev, const lv_point_t * points)
{
    if(indev->driver.type == LV_INDEV_TYPE_BUTTON) {
        indev->custom_data.btn_points = points;
    }
}

/**
 * Set feedback callback for indev.
 * @param indev pointer to an input device
 * @param feedback feedback callback
 */
void lv_indev_set_feedback(lv_indev_t *indev, lv_indev_feedback_t feedback)
{
	indev->feedback = feedback;
}

/**
 * Get the last point of an input device (for LV_INDEV_TYPE_POINTER and LV_INDEV_TYPE_BUTTON)
 * @param indev pointer to an input device
 * @param point pointer to a point to store the result
 */
void lv_indev_get_point(const lv_indev_t * indev, lv_point_t * point)
{
    if(indev->driver.type != LV_INDEV_TYPE_POINTER && indev->driver.type != LV_INDEV_TYPE_BUTTON) {
        point->x = -1;
        point->y = -1;
    } else {
        point->x = indev->proc.types.pointer.act_point.x;
        point->y = indev->proc.types.pointer.act_point.y;
    }
}

/**
 * Get the last key of an input device (for LV_INDEV_TYPE_KEYPAD)
 * @param indev pointer to an input device
 * @return the last pressed key (0 on error)
 */
uint32_t lv_indev_get_key(const lv_indev_t * indev)
{
    if(indev->driver.type != LV_INDEV_TYPE_KEYPAD) return 0;
    else return indev->proc.types.keypad.last_key;
}

/**
 * Check if there is dragging with an input device or not (for LV_INDEV_TYPE_POINTER and LV_INDEV_TYPE_BUTTON)
 * @param indev pointer to an input device
 * @return true: drag is in progress
 */
bool lv_indev_is_dragging(const lv_indev_t * indev)
{
    if(indev == NULL) return false;
    if(indev->driver.type != LV_INDEV_TYPE_POINTER && indev->driver.type != LV_INDEV_TYPE_BUTTON) return false;
    return indev->proc.types.pointer.drag_in_prog == 0 ? false : true;
}

/**
 * Get the types.pointer.vector of dragging of an input device (for LV_INDEV_TYPE_POINTER and LV_INDEV_TYPE_BUTTON)
 * @param indev pointer to an input device
 * @param point pointer to a point to store the types.pointer.vector
 */
void lv_indev_get_vect(const lv_indev_t * indev, lv_point_t * point)
{
    if(indev == NULL) {
        point->x = 0;
        point->y = 0;
        return;
    }

    if(indev->driver.type != LV_INDEV_TYPE_POINTER && indev->driver.type != LV_INDEV_TYPE_BUTTON) {
        point->x = 0;
        point->y = 0;
    } else {
        point->x = indev->proc.types.pointer.vect.x;
        point->y = indev->proc.types.pointer.vect.y;
    }
}

/**
 * Get elapsed time since last press
 * @param indev pointer to an input device (NULL to get the overall smallest inactivity)
 * @return Elapsed ticks (milliseconds) since last press
 */
uint32_t lv_indev_get_inactive_time(const lv_indev_t * indev)
{

    uint32_t t;

    if(indev) return t = lv_tick_elaps(indev->last_activity_time);

    lv_indev_t * i;
    t = UINT16_MAX;
    i = lv_indev_next(NULL);
    while(i) {
        t = LV_MATH_MIN(t, lv_tick_elaps(i->last_activity_time));
        i = lv_indev_next(i);
    }

    return t;
}

/**
 * Get feedback callback for indev.
 * @param indev pointer to an input device
 * @return feedback callback
 */
lv_indev_feedback_t lv_indev_get_feedback(const lv_indev_t *indev)
{
	return indev->feedback;
}

/**
 * Do nothing until the next release
 * @param indev pointer to an input device
 */
void lv_indev_wait_release(lv_indev_t * indev)
{
    indev->proc.types.pointer.wait_unil_release = 1;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#if LV_INDEV_READ_PERIOD != 0
/**
 * Called periodically to handle the input devices
 * @param param unused
 */
static void indev_proc_task(void * param)
{
    (void)param;


    LV_LOG_TRACE("indev task started");

    lv_indev_data_t data;
    lv_indev_t * i;
    i = lv_indev_next(NULL);

    /*Read and process all indevs*/
    while(i) {
        indev_act = i;

        /*Handle reset query before processing the point*/
        indev_proc_reset_query_handler(i);

        if(i->proc.disabled == 0) {
            bool more_to_read;
            do {
                /*Read the data*/
                more_to_read = lv_indev_read(i, &data);
                indev_proc_reset_query_handler(i);          /*The active object might deleted even in the read function*/
                i->proc.state = data.state;

                if(i->proc.state == LV_INDEV_STATE_PR) {
                    i->last_activity_time = lv_tick_get();
                }

                if(i->driver.type == LV_INDEV_TYPE_POINTER) {
                    indev_pointer_proc(i, &data);
                } else if(i->driver.type == LV_INDEV_TYPE_KEYPAD) {
                    indev_keypad_proc(i, &data);
                } else if(i->driver.type == LV_INDEV_TYPE_ENCODER) {
                    indev_encoder_proc(i, &data);
                } else if(i->driver.type == LV_INDEV_TYPE_BUTTON) {
                    indev_button_proc(i, &data);
                }
                /*Handle reset query if it happened in during processing*/
                indev_proc_reset_query_handler(i);
            } while(more_to_read);
        }
        i = lv_indev_next(i);    /*Go to the next indev*/
    }

    indev_act = NULL;   /*End of indev processing, so no act indev*/

    LV_LOG_TRACE("indev task finished");
}


/**
 * Process a new point from LV_INDEV_TYPE_POINTER input device
 * @param i pointer to an input device
 * @param data pointer to the data read from the input device
 */
static void indev_pointer_proc(lv_indev_t * i, lv_indev_data_t * data)
{
    /*Move the cursor if set and moved*/
    if(i->custom_data.cursor != NULL &&
            (i->proc.types.pointer.last_point.x != data->point.x ||
             i->proc.types.pointer.last_point.y != data->point.y)) {
        lv_obj_set_pos(i->custom_data.cursor, data->point.x, data->point.y);
    }

    i->proc.types.pointer.act_point.x = data->point.x;
    i->proc.types.pointer.act_point.y = data->point.y;

    if(i->proc.state == LV_INDEV_STATE_PR) {
        indev_proc_press(&i->proc);
    } else {
        indev_proc_release(&i->proc);
    }

    i->proc.types.pointer.last_point.x = i->proc.types.pointer.act_point.x;
    i->proc.types.pointer.last_point.y = i->proc.types.pointer.act_point.y;
}

/**
 * Process a new point from LV_INDEV_TYPE_KEYPAD input device
 * @param i pointer to an input device
 * @param data pointer to the data read from the input device
 */
static void indev_keypad_proc(lv_indev_t * i, lv_indev_data_t * data)
{
#if USE_LV_GROUP
    lv_group_t * g = i->custom_data.group;
    if(g == NULL) return;

    lv_obj_t * focused = lv_group_get_focused(g);

    /*Key press happened*/
    if(data->state == LV_INDEV_STATE_PR &&
            i->proc.types.keypad.last_state == LV_INDEV_STATE_REL) {
        i->proc.pr_timestamp = lv_tick_get();

        lv_obj_t * focused = lv_group_get_focused(g);
        if(focused && data->key == LV_GROUP_KEY_ENTER) {
            focused->signal_cb(focused, LV_SIGNAL_PRESSED, indev_act);
            lv_obj_send_event(focused, LV_EVENT_PRESSED);
        }
    }
    /*Pressing*/
    else if(data->state == LV_INDEV_STATE_PR && i->proc.types.keypad.last_state == LV_INDEV_STATE_PR) {
        if(data->key == LV_GROUP_KEY_ENTER &&
                i->proc.long_pr_sent == 0 &&
                lv_tick_elaps(i->proc.pr_timestamp) > LV_INDEV_LONG_PRESS_TIME) {
            /*On enter long press leave edit mode.*/
            lv_obj_t * focused = lv_group_get_focused(g);
            if(focused) {
                focused->signal_cb(focused, LV_SIGNAL_LONG_PRESS, indev_act);
                i->proc.long_pr_sent = 1;
                lv_obj_send_event(focused, LV_EVENT_LONG_PRESSED);
            }
        }
    }
    /*Release happened*/
    else if(data->state == LV_INDEV_STATE_REL && i->proc.types.keypad.last_state == LV_INDEV_STATE_PR) {
        /*The user might clear the key when it was released. Always release the pressed key*/
        data->key = i->proc.types.keypad.last_key;

        /* Edit mode is not used by KEYPAD devices.
         * So leave edit mode if we are in it before focusing on the next/prev object*/
        if(data->key == LV_GROUP_KEY_NEXT || data->key == LV_GROUP_KEY_PREV) {
            lv_group_set_editing(g, false);
        }

        if(data->key == LV_GROUP_KEY_NEXT) {
            lv_group_focus_next(g);
        } else if(data->key == LV_GROUP_KEY_PREV) {
            lv_group_focus_prev(g);
        } else if(data->key == LV_GROUP_KEY_ENTER) {
            if(!i->proc.long_pr_sent) {
                focused->signal_cb(focused, LV_SIGNAL_RELEASED, indev_act);
                lv_obj_send_event(focused, LV_EVENT_CLICKED);
            }
        } else {
            lv_group_send_data(g, data->key);
        }

        if(i->proc.reset_query) return;     /*The object might be deleted in `focus_cb` or due to any other user event*/

        i->proc.pr_timestamp = 0;
        i->proc.long_pr_sent = 0;
    }

    i->proc.types.keypad.last_state = data->state;
    i->proc.types.keypad.last_key = data->key;
#else
    (void)data; /*Unused*/
    (void)i; /*Unused*/
#endif
}

/**
 * Process a new point from LV_INDEV_TYPE_ENCODER input device
 * @param i pointer to an input device
 * @param data pointer to the data read from the input device
 */
static void indev_encoder_proc(lv_indev_t * i, lv_indev_data_t * data)
{
#if USE_LV_GROUP
    lv_group_t * g = i->custom_data.group;

    if(g == NULL) return;

    /*Process the steps first. They are valid only with released button*/
    if(data->state == LV_INDEV_STATE_REL) {
        /*In edit mode send LEFT/RIGHT keys*/
        if(lv_group_get_editing(g)) {
            int32_t s;
            if(data->enc_diff < 0) {
                for(s = 0; s < -data->enc_diff; s++) lv_group_send_data(g, LV_GROUP_KEY_LEFT);
            } else if(data->enc_diff > 0) {
                for(s = 0; s < data->enc_diff; s++) lv_group_send_data(g, LV_GROUP_KEY_RIGHT);
            }
        }
        /*In navigate mode focus on the next/prev objects*/
        else {
            int32_t s;
            if(data->enc_diff < 0) {
                for(s = 0; s < -data->enc_diff; s++) lv_group_focus_prev(g);
            } else if(data->enc_diff > 0) {
                for(s = 0; s < data->enc_diff; s++) lv_group_focus_next(g);
            }
        }
    }

    /*Key press happened*/
    if(data->state == LV_INDEV_STATE_PR &&
            i->proc.types.keypad.last_state == LV_INDEV_STATE_REL) {
        i->proc.pr_timestamp = lv_tick_get();
    }
    /*Pressing*/
    else if(data->state == LV_INDEV_STATE_PR && i->proc.types.keypad.last_state == LV_INDEV_STATE_PR) {
        if(i->proc.long_pr_sent == 0 &&
                lv_tick_elaps(i->proc.pr_timestamp) > LV_INDEV_LONG_PRESS_TIME) {
            /*On enter long press leave edit mode.*/
            lv_obj_t * focused = lv_group_get_focused(g);

            bool editable = false;
            if(focused) focused->signal_cb(focused, LV_SIGNAL_GET_EDITABLE, &editable);

            if(editable) {
                if(g->obj_ll.head != g->obj_ll.tail)
                    lv_group_set_editing(g, lv_group_get_editing(g) ? false : true);  /*Toggle edit mode on long press*/
                else if(focused)
                    focused->signal_cb(focused, LV_SIGNAL_LONG_PRESS, indev_act);
            }
            /*If not editable then just send a long press signal*/
            else {
                if(focused)
                    focused->signal_cb(focused, LV_SIGNAL_LONG_PRESS, indev_act);
            }
            i->proc.long_pr_sent = 1;
        }
    }
    /*Release happened*/
    else if(data->state == LV_INDEV_STATE_REL && i->proc.types.keypad.last_state == LV_INDEV_STATE_PR) {
        lv_obj_t * focused = lv_group_get_focused(g);
        bool editable = false;
        if(focused) focused->signal_cb(focused, LV_SIGNAL_GET_EDITABLE, &editable);

        /*The button was released on a non-editable object. Just send enter*/
        if(!editable) {
            lv_group_send_data(g, LV_GROUP_KEY_ENTER);
        }
        /*An object is being edited and the button is releases. Just send enter */
        else if(g->editing) {
            if(!i->proc.long_pr_sent || g->obj_ll.head == g->obj_ll.tail)
                lv_group_send_data(g, LV_GROUP_KEY_ENTER);  /*Ignore long pressed enter release because it comes from mode switch*/
        }
        /*If the focused object is editable and now in navigate mode then enter edit mode*/
        else if(editable && !g->editing && !i->proc.long_pr_sent) {
            lv_group_set_editing(g, lv_group_get_editing(g) ? false : true);  /*Toggle edit mode on long press*/
        }

        if(i->proc.reset_query) return;     /*The object might be deleted in `focus_cb` or due to any other user event*/

        i->proc.pr_timestamp = 0;
        i->proc.long_pr_sent = 0;
    }

    i->proc.types.keypad.last_state = data->state;
    i->proc.types.keypad.last_key = data->key;
#else
    (void)data; /*Unused*/
    (void)i; /*Unused*/
#endif
}

/**
 * Process new points from a input device. indev->state.pressed has to be set
 * @param indev pointer to an input device state
 * @param x x coordinate of the next point
 * @param y y coordinate of the next point
 */
static void indev_button_proc(lv_indev_t * i, lv_indev_data_t * data)
{
    i->proc.types.pointer.act_point.x = i->custom_data.btn_points[data->btn_id].x;
    i->proc.types.pointer.act_point.y = i->custom_data.btn_points[data->btn_id].y;

    /*Still the same point is pressed*/
    if(i->proc.types.pointer.last_point.x == i->proc.types.pointer.act_point.x &&
            i->proc.types.pointer.last_point.y == i->proc.types.pointer.act_point.y &&
            data->state == LV_INDEV_STATE_PR)
    {
        indev_proc_press(&i->proc);
    } else {
        /*If a new point comes always make a release*/
        indev_proc_release(&i->proc);
    }

    i->proc.types.pointer.last_point.x = i->proc.types.pointer.act_point.x;
    i->proc.types.pointer.last_point.y = i->proc.types.pointer.act_point.y;
}

/**
 * Process the pressed state of LV_INDEV_TYPE_POINER input devices
 * @param indev pointer to an input device 'proc'
 */
static void indev_proc_press(lv_indev_proc_t * proc)
{
    lv_obj_t * pr_obj = proc->types.pointer.act_obj;

    if(proc->types.pointer.wait_unil_release != 0) return;

    lv_disp_t * disp = indev_act->driver.disp;

    /*If there is no last object then search*/
    if(proc->types.pointer.act_obj == NULL) {
        pr_obj = indev_search_obj(proc, lv_disp_get_layer_sys(disp));
        if(pr_obj == NULL) pr_obj = indev_search_obj(proc, lv_disp_get_layer_top(disp));
        if(pr_obj == NULL) pr_obj = indev_search_obj(proc, lv_disp_get_scr_act(disp));
    }
    /*If there is last object but it is not dragged and not protected also search*/
    else if(proc->types.pointer.drag_in_prog == 0 &&
            lv_obj_is_protected(proc->types.pointer.act_obj, LV_PROTECT_PRESS_LOST) == false) {/*Now types.pointer.act_obj != NULL*/
        pr_obj = indev_search_obj(proc, lv_disp_get_layer_sys(disp));
        if(pr_obj == NULL) pr_obj = indev_search_obj(proc, lv_disp_get_layer_top(disp));
        if(pr_obj == NULL) pr_obj = indev_search_obj(proc, lv_disp_get_scr_act(disp));
    }
    /*If a dragable or a protected object was the last then keep it*/
    else {

    }

    /*If a new object was found reset some variables and send a pressed signal*/
    if(pr_obj != proc->types.pointer.act_obj) {

        proc->types.pointer.last_point.x = proc->types.pointer.act_point.x;
        proc->types.pointer.last_point.y = proc->types.pointer.act_point.y;

        /*If a new object found the previous was lost, so send a signal*/
        if(proc->types.pointer.act_obj != NULL) {
            proc->types.pointer.act_obj->signal_cb(proc->types.pointer.act_obj, LV_SIGNAL_PRESS_LOST, indev_act);
            lv_obj_send_event(proc->types.pointer.act_obj, LV_EVENT_PRESS_LOST);
            if(proc->reset_query != 0) return;
        }

        proc->types.pointer.act_obj = pr_obj;           /*Save the pressed object*/
        proc->types.pointer.last_obj = proc->types.pointer.act_obj;   /*Refresh the types.pointer.last_obj*/

        if(proc->types.pointer.act_obj != NULL) {
            /* Save the time when the obj pressed.
             * It is necessary to count the long press time.*/
            proc->pr_timestamp = lv_tick_get();
            proc->long_pr_sent = 0;
            proc->types.pointer.drag_limit_out = 0;
            proc->types.pointer.drag_in_prog = 0;
            proc->types.pointer.drag_sum.x = 0;
            proc->types.pointer.drag_sum.y = 0;
            proc->types.pointer.vect.x = 0;
            proc->types.pointer.vect.y = 0;

            /*Search for 'top' attribute*/
            lv_obj_t * i = proc->types.pointer.act_obj;
            lv_obj_t * last_top = NULL;
            while(i != NULL) {
                if(i->top != 0) last_top = i;
                i = lv_obj_get_parent(i);
            }

            if(last_top != NULL) {
                /*Move the last_top object to the foreground*/
                lv_obj_t * par = lv_obj_get_parent(last_top);
                /*After list change it will be the new head*/
                lv_ll_chg_list(&par->child_ll, &par->child_ll, last_top);
                lv_obj_invalidate(last_top);
            }

            /*Send a signal about the press*/
            proc->types.pointer.act_obj->signal_cb(proc->types.pointer.act_obj, LV_SIGNAL_PRESSED, indev_act);
            lv_obj_send_event(proc->types.pointer.act_obj, LV_EVENT_PRESSED);
            if(proc->reset_query != 0) return;
        }
    }

    /*Calculate the types.pointer.vector*/
    proc->types.pointer.vect.x = proc->types.pointer.act_point.x - proc->types.pointer.last_point.x;
    proc->types.pointer.vect.y = proc->types.pointer.act_point.y - proc->types.pointer.last_point.y;

    proc->types.pointer.drag_throw_vect.x = (proc->types.pointer.drag_throw_vect.x * 5) >> 3;
    proc->types.pointer.drag_throw_vect.y = (proc->types.pointer.drag_throw_vect.y * 5) >> 3;

    if(proc->types.pointer.drag_throw_vect.x < 0) proc->types.pointer.drag_throw_vect.x++;
    else if(proc->types.pointer.drag_throw_vect.x > 0) proc->types.pointer.drag_throw_vect.x--;

    if(proc->types.pointer.drag_throw_vect.y < 0) proc->types.pointer.drag_throw_vect.y++;
    else if(proc->types.pointer.drag_throw_vect.y > 0) proc->types.pointer.drag_throw_vect.y--;

    proc->types.pointer.drag_throw_vect.x += (proc->types.pointer.vect.x * 4) >> 3;
    proc->types.pointer.drag_throw_vect.y += (proc->types.pointer.vect.y * 4) >> 3;

    /*If there is active object and it can be dragged run the drag*/
    if(proc->types.pointer.act_obj != NULL) {
        proc->types.pointer.act_obj->signal_cb(proc->types.pointer.act_obj, LV_SIGNAL_PRESSING, indev_act);
        lv_obj_send_event(proc->types.pointer.act_obj, LV_EVENT_PRESSING);
        if(proc->reset_query != 0) return;

        indev_drag(proc);
        if(proc->reset_query != 0) return;

        /*If there is no drag then check for long press time*/
        if(proc->types.pointer.drag_in_prog == 0 && proc->long_pr_sent == 0) {
            /*Send a signal about the long press if enough time elapsed*/
            if(lv_tick_elaps(proc->pr_timestamp) > LV_INDEV_LONG_PRESS_TIME) {
                pr_obj->signal_cb(pr_obj, LV_SIGNAL_LONG_PRESS, indev_act);
                lv_obj_send_event(pr_obj, LV_EVENT_LONG_PRESSED);
                if(proc->reset_query != 0) return;

                /*Mark the signal sending to do not send it again*/
                proc->long_pr_sent = 1;

                /*Save the long press time stamp for the long press repeat handler*/
                proc->longpr_rep_timestamp = lv_tick_get();
            }
        }
        /*Send long press repeated signal*/
        if(proc->types.pointer.drag_in_prog == 0 && proc->long_pr_sent == 1) {
            /*Send a signal about the long press repeate if enough time elapsed*/
            if(lv_tick_elaps(proc->longpr_rep_timestamp) > LV_INDEV_LONG_PRESS_REP_TIME) {
                pr_obj->signal_cb(pr_obj, LV_SIGNAL_LONG_PRESS_REP, indev_act);
                lv_obj_send_event(pr_obj, LV_EVENT_LONG_PRESSED_REPEAT);
                if(proc->reset_query != 0) return;
                proc->longpr_rep_timestamp = lv_tick_get();

            }
        }
    }
}

/**
 * Process the released state of LV_INDEV_TYPE_POINER input devices
 * @param proc pointer to an input device 'proc'
 */
static void indev_proc_release(lv_indev_proc_t * proc)
{
    if(proc->types.pointer.wait_unil_release != 0) {
        proc->types.pointer.act_obj = NULL;
        proc->types.pointer.last_obj = NULL;
        proc->pr_timestamp = 0;
        proc->longpr_rep_timestamp = 0;
        proc->types.pointer.wait_unil_release = 0;
    }

    /*Forgot the act obj and send a released signal */
    if(proc->types.pointer.act_obj != NULL) {
        /* If the object was protected against press lost then it possible that
         * the object is already not pressed but still it is the `types.pointer.act_obj`.
         * In this case send the `LV_SIGNAL_RELEASED` if the indev is ON the `types.pointer.act_obj` */
        if(lv_obj_is_protected(proc->types.pointer.act_obj, LV_PROTECT_PRESS_LOST)) {
            /* Search the object on the current current coordinates.
             * The start object is the object itself. If not ON it the the result will be NULL*/
            lv_obj_t * obj_on = indev_search_obj(proc, proc->types.pointer.act_obj);
            if(obj_on == proc->types.pointer.act_obj) {
                proc->types.pointer.act_obj->signal_cb(proc->types.pointer.act_obj, LV_SIGNAL_RELEASED, indev_act);
                if(proc->long_pr_sent == 0 && proc->types.pointer.drag_in_prog == 0) {
                    lv_obj_send_event(proc->types.pointer.act_obj, LV_EVENT_CLICKED);
                } else {
                    lv_obj_send_event(proc->types.pointer.act_obj, LV_EVENT_RELEASED);
                }
            }
            else {
                proc->types.pointer.act_obj->signal_cb(proc->types.pointer.act_obj, LV_SIGNAL_PRESS_LOST, indev_act);
                lv_obj_send_event(proc->types.pointer.act_obj, LV_SIGNAL_PRESS_LOST);
            }
        }
        /* The simple case: `types.pointer.act_obj` was not protected against press lost.
         * If it is already not pressed then was handled in `indev_proc_press`*/
        else {
            proc->types.pointer.act_obj->signal_cb(proc->types.pointer.act_obj, LV_SIGNAL_RELEASED, indev_act);

            if(proc->long_pr_sent == 0 && proc->types.pointer.drag_in_prog == 0) {
                lv_obj_send_event(proc->types.pointer.act_obj, LV_EVENT_CLICKED);
            } else {
                lv_obj_send_event(proc->types.pointer.act_obj, LV_SIGNAL_RELEASED);
            }
        }

        if(proc->reset_query != 0) return;

        /*Handle click focus*/
#if USE_LV_GROUP
        /*Edit mode is not used by POINTER devices. So leave edit mode if we are in it*/
        lv_group_t * act_g = lv_obj_get_group(proc->types.pointer.act_obj);
        if(lv_group_get_editing(act_g)) {
            lv_group_set_editing(act_g, false);
        }

        /*Check, if the parent is in a group focus on it.*/
        if(lv_obj_is_protected(proc->types.pointer.act_obj, LV_PROTECT_CLICK_FOCUS) == false) {       /*Respect the click protection*/
            lv_group_t * g = lv_obj_get_group(proc->types.pointer.act_obj);
            lv_obj_t * parent = proc->types.pointer.act_obj;

            while(g == NULL) {
                parent = lv_obj_get_parent(parent);
                if(parent == NULL) break;
                if(lv_obj_is_protected(parent, LV_PROTECT_CLICK_FOCUS)) {   /*Ignore is the protected against click focus*/
                    parent = NULL;
                    break;
                }
                g = lv_obj_get_group(parent);
            }

            if(g != NULL && parent != NULL)
                if(lv_group_get_click_focus(g)) {
                    lv_group_focus_obj(parent);
                }
        }
#endif

        if(proc->reset_query != 0) return;
        proc->types.pointer.act_obj = NULL;
        proc->pr_timestamp = 0;
        proc->longpr_rep_timestamp = 0;
    }

    /*The reset can be set in the signal function.
     * In case of reset query ignore the remaining parts.*/
    if(proc->types.pointer.last_obj != NULL && proc->reset_query == 0) {
        indev_drag_throw(proc);
        if(proc->reset_query != 0) return;
    }
}

/**
 * Process a new point from LV_INDEV_TYPE_BUTTON input device
 * @param i pointer to an input device
 * @param data pointer to the data read from the input device
 * Reset input device if a reset query has been sent to it
 * @param indev pointer to an input device
 */
static void indev_proc_reset_query_handler(lv_indev_t * indev)
{
    if(indev->proc.reset_query) {
        indev->proc.types.pointer.act_obj = NULL;
        indev->proc.types.pointer.last_obj = NULL;
        indev->proc.types.pointer.drag_limit_out = 0;
        indev->proc.types.pointer.drag_in_prog = 0;
        indev->proc.long_pr_sent = 0;
        indev->proc.pr_timestamp = 0;
        indev->proc.longpr_rep_timestamp = 0;
        indev->proc.types.pointer.drag_sum.x = 0;
        indev->proc.types.pointer.drag_sum.y = 0;
        indev->proc.types.pointer.drag_throw_vect.x = 0;
        indev->proc.types.pointer.drag_throw_vect.y = 0;
        indev->proc.reset_query = 0;
    }
}
/**
 * Search the most top, clickable object on the last point of an input device
 * @param proc pointer to  the `lv_indev_proc_t` part of the input device
 * @param obj pointer to a start object, typically the screen
 * @return pointer to the found object or NULL if there was no suitable object
 */
static lv_obj_t * indev_search_obj(const lv_indev_proc_t * proc, lv_obj_t * obj)
{
    lv_obj_t * found_p = NULL;

    /*If the point is on this object*/
    /*Check its children too*/
    if(lv_area_is_point_on(&obj->coords, &proc->types.pointer.act_point)) {
        lv_obj_t * i;

        LL_READ(obj->child_ll, i) {
            found_p = indev_search_obj(proc, i);

            /*If a child was found then break*/
            if(found_p != NULL) {
                break;
            }
        }

        /*If then the children was not ok, and this obj is clickable
         * and it or its parent is not hidden then save this object*/
        if(found_p == NULL && lv_obj_get_click(obj) != false) {
            lv_obj_t * hidden_i = obj;
            while(hidden_i != NULL) {
                if(lv_obj_get_hidden(hidden_i) == true) break;
                hidden_i = lv_obj_get_parent(hidden_i);
            }
            /*No parent found with hidden == true*/
            if(hidden_i == NULL) found_p = obj;
        }

    }

    return found_p;
}

/**
 * Handle the dragging of indev_proc_p->types.pointer.act_obj
 * @param indev pointer to a input device state
 */
static void indev_drag(lv_indev_proc_t * state)
{
    lv_obj_t * drag_obj = state->types.pointer.act_obj;

    /*If drag parent is active check recursively the drag_parent attribute*/
    while(lv_obj_get_drag_parent(drag_obj) != false &&
            drag_obj != NULL) {
        drag_obj = lv_obj_get_parent(drag_obj);
    }

    if(drag_obj == NULL) return;

    if(lv_obj_get_drag(drag_obj) == false) return;

    /*Count the movement by drag*/
    state->types.pointer.drag_sum.x += state->types.pointer.vect.x;
    state->types.pointer.drag_sum.y += state->types.pointer.vect.y;

    /*Enough move?*/
    if(state->types.pointer.drag_limit_out == 0) {
        /*If a move is greater then LV_DRAG_LIMIT then begin the drag*/
        if(LV_MATH_ABS(state->types.pointer.drag_sum.x) >= LV_INDEV_DRAG_LIMIT ||
                LV_MATH_ABS(state->types.pointer.drag_sum.y) >= LV_INDEV_DRAG_LIMIT) {
            state->types.pointer.drag_limit_out = 1;
        }
    }

    /*If the drag limit is exceeded handle the dragging*/
    if(state->types.pointer.drag_limit_out != 0) {
        /*Set new position if the vector is not zero*/
        if(state->types.pointer.vect.x != 0 ||
                state->types.pointer.vect.y != 0)
        {

            /*Get the coordinates of the object and modify them*/
            lv_coord_t act_x = lv_obj_get_x(drag_obj);
            lv_coord_t act_y = lv_obj_get_y(drag_obj);
            uint16_t inv_buf_size = lv_disp_get_inv_buf_size(indev_act->driver.disp); /*Get the number of currently invalidated areas*/

            lv_coord_t prev_x = drag_obj->coords.x1;
            lv_coord_t prev_y = drag_obj->coords.y1;
            lv_coord_t prev_par_w = lv_obj_get_width(lv_obj_get_parent(drag_obj));
            lv_coord_t prev_par_h = lv_obj_get_height(lv_obj_get_parent(drag_obj));

            lv_obj_set_pos(drag_obj, act_x + state->types.pointer.vect.x, act_y + state->types.pointer.vect.y);

            /*Set the drag in progress flag if the object is really moved*/
            if(drag_obj->coords.x1 != prev_x || drag_obj->coords.y1 != prev_y) {
                if(state->types.pointer.drag_in_prog != 0) { /*Send the drag begin signal on first move*/
                    drag_obj->signal_cb(drag_obj,  LV_SIGNAL_DRAG_BEGIN, indev_act);

                    if(state->reset_query != 0) return;
                }
                state->types.pointer.drag_in_prog = 1;
            }
            /*If the object didn't moved then clear the invalidated areas*/
            else {
                /*In a special case if the object is moved on a page and
                 * the scrollable has fit == true and the object is dragged of the page then
                 * while its coordinate is not changing only the parent's size is reduced */
                lv_coord_t act_par_w = lv_obj_get_width(lv_obj_get_parent(drag_obj));
                lv_coord_t act_par_h = lv_obj_get_height(lv_obj_get_parent(drag_obj));
                if(act_par_w == prev_par_w && act_par_h == prev_par_h) {
                    uint16_t new_inv_buf_size = lv_disp_get_inv_buf_size(indev_act->driver.disp);
                    lv_disp_pop_from_inv_buf(indev_act->driver.disp, new_inv_buf_size - inv_buf_size);
                }
            }
        }
    }
}

/**
 * Handle throwing by drag if the drag is ended
 * @param indev pointer to an input device state
 */
static void indev_drag_throw(lv_indev_proc_t * proc)
{
    if(proc->types.pointer.drag_in_prog == 0) return;

    /*Set new position if the vector is not zero*/
    lv_obj_t * drag_obj = proc->types.pointer.last_obj;

    /*If drag parent is active check recursively the drag_parent attribute*/
    while(lv_obj_get_drag_parent(drag_obj) != false &&
            drag_obj != NULL) {
        drag_obj = lv_obj_get_parent(drag_obj);
    }

    if(drag_obj == NULL) {
        return;
    }

    /*Return if the drag throw is not enabled*/
    if(lv_obj_get_drag_throw(drag_obj) == false) {
        proc->types.pointer.drag_in_prog = 0;
        drag_obj->signal_cb(drag_obj, LV_SIGNAL_DRAG_END, indev_act);
        return;
    }

    /*Reduce the vectors*/
    proc->types.pointer.drag_throw_vect.x = proc->types.pointer.drag_throw_vect.x * (100 - LV_INDEV_DRAG_THROW) / 100;
    proc->types.pointer.drag_throw_vect.y = proc->types.pointer.drag_throw_vect.y * (100 - LV_INDEV_DRAG_THROW) / 100;

    if(proc->types.pointer.drag_throw_vect.x != 0 ||
            proc->types.pointer.drag_throw_vect.y != 0) {
        /*Get the coordinates and modify them*/
        lv_area_t coords_ori;
        lv_obj_get_coords(drag_obj, &coords_ori);
        lv_coord_t act_x = lv_obj_get_x(drag_obj) + proc->types.pointer.drag_throw_vect.x;
        lv_coord_t act_y = lv_obj_get_y(drag_obj) + proc->types.pointer.drag_throw_vect.y;
        lv_obj_set_pos(drag_obj, act_x, act_y);

        lv_area_t coord_new;
        lv_obj_get_coords(drag_obj, &coord_new);

        /*If non of the coordinates are changed then do not continue throwing*/
        if((coords_ori.x1 == coord_new.x1 || proc->types.pointer.drag_throw_vect.x == 0) &&
                (coords_ori.y1 == coord_new.y1 || proc->types.pointer.drag_throw_vect.y == 0)) {
            proc->types.pointer.drag_in_prog = 0;
            proc->types.pointer.vect.x = 0;
            proc->types.pointer.vect.y = 0;
            proc->types.pointer.drag_throw_vect.x = 0;
            proc->types.pointer.drag_throw_vect.y = 0;
            drag_obj->signal_cb(drag_obj, LV_SIGNAL_DRAG_END, indev_act);

        }
    }
    /*If the types.pointer.vectors become 0 -> types.pointer.drag_in_prog = 0 and send a drag end signal*/
    else {
        proc->types.pointer.drag_in_prog = 0;
        drag_obj->signal_cb(drag_obj, LV_SIGNAL_DRAG_END, indev_act);
    }

}
#endif
