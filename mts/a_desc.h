#pragma once

#include <stdio.h>
#include <stdlib.h>
#include "std_template/simple_hash_map.h"
#include "std_template/simple_list.h"
#include "std_template/simple_string.h"
#include "std_port/std_port.h"
#include "std_port/std_port_type.h"
#include "std_memmgr/std_memmgr.h"
#include "cmm_call.h"
#include "cmm_domain.h"
#include "cmm_object.h"
#include "cmm_memory.h"
#include "cmm_thread.h"
#include "cmm_value.h"

namespace cmm
{

class __feature_desc_impl : public AbstractComponent
{
public:
    Value print(Thread *__thread, Object *__this_object, ComponentNo __component_no, Value *__args, ArgNo __n)
    {
        if (__n != 0)
            throw simple::string().snprintf("Bad parameters, expected %d, got %d.", 1, __n);

        CallContextNode __context(__thread, __this_object, __component_no, __args, __n, (Value*)0, 0);
        __thread->enter_function_call(&__context);

        printf("Name: %s.\n", call_far(__thread, 1 /* Component:Name */, 1 /* get_name() */).m_string->s.c_str());

        return __thread->leave_function_call(&__context, Value());
    }
};

}
