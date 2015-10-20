// cmm_thread.cpp

#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include "std_port/std_port_compiler.h"
#include "std_port/std_port_os.h"
#include "cmm_domain.h"
#include "cmm_object.h"
#include "cmm_thread.h"

namespace cmm
{

Thread::GetStackPointerFunc Thread::m_get_stack_pointer_func = 0;

// Constructor for Thread start CallContext only
CallContext::CallContext(Thread *thread) :
    m_prev_context(0),
    m_domain(thread->m_start_domain),
    m_arg(0),
    m_arg_count(0),
    m_local(0),
    m_local_count(0),
    m_this_object(0),
    m_this_component(0),
    m_start_sp((void **)&thread + 4 /* Add 4 words */),
    m_end_sp((void *)&thread),
    m_thread(thread)
{
    STD_ASSERT(("This is top CallContext, m_prev_context must be 0.", !thread->m_context));
    STD_ASSERT(("For top CallContext, current domain must be m_start_domain.", m_domain == thread->get_current_domain()));
    m_domain_object = 0;

    // Link to thread
    CallContextNode *node;
    void *ptr_node = ((Uint8 *)this) - offsetof(CallContextNode, value);
    node = static_cast<CallContextNode *>(ptr_node);
    thread->m_context = node;
}

// Constructor
CallContext::CallContext(Thread *thread,
    Object *this_object, ComponentNo this_component,
    Value *arg, ArgNo arg_count,
    Value *local, ArgNo local_count) :
    m_prev_context(thread->m_context),
    m_domain(thread->get_current_domain()),
    m_arg(arg),
    m_arg_count(arg_count),
    m_local(local),
    m_local_count(local_count),
    m_this_object(this_object),
    m_this_component(this_component),
    m_start_sp((void *)(arg + arg_count)),
    m_end_sp((void *)&local_count),
    m_thread(thread)
{
    // Update end_sp of previous context
    STD_ASSERT(("There must be existed m_prev_context.", m_prev_context));
    m_prev_context->value.m_end_sp = &local_count;

    // Save object in domain in this context
    if (this_object && this_object->get_domain())
        m_domain_object = this_object;
    else
        m_domain_object = m_prev_context->value.m_domain_object;

    // Link to thread
    CallContextNode *node;
    void *ptr_node = ((Uint8 *)this) - offsetof(CallContextNode, value);
    node = static_cast<CallContextNode *>(ptr_node);
    thread->m_context = node;
}

// CallContext has no destructor, use this routine instead
void CallContext::pop_context()
{
}

std_tls_t Thread::m_thread_tls_id = STD_NO_TLS_ID;

// Initialize this module
int Thread::init()
{
    std_allocate_tls(&m_thread_tls_id);

    // Set handler - This is only to prevent optimization
    m_get_stack_pointer_func = (GetStackPointerFunc)([]() { void *p = (void *)&p; return p; });

    // Start current thread
    Thread *thread = XNEW(Thread);
    thread->start();

    return 0;
}

// Shutdown this moudule
void Thread::shutdown()
{
    // Stop current thread
    Thread *thread = Thread::get_current_thread();
    thread->stop();
    XDELETE(thread);

    std_free_tls(m_thread_tls_id);
}

Thread::Thread(simple::string name)
{
    // Set default values
    m_context = 0;
    m_current_domain = 0;

    // Set name to this thread
    if (name.length())
        m_name = name;
    else
        m_name.snprintf("(%zu)", 64, (size_t) std_get_current_task_id());
}

Thread::~Thread()
{
    // Thread is closed
    if (m_value_list.get_count())
    {
        printf("There %zu valus still alive in thread %s.\n",
               m_value_list.get_count(),
               m_name.c_str());
        auto *p = m_value_list.get_list();
        while (p)
        {
            auto *current = p;
            p = p->next;
            XDELETE(current);
        }
        m_value_list.reset();
    }
}

// Thread is started
void Thread::start()
{
    auto existed = (Thread *) std_get_tls_data(m_thread_tls_id);
    if (existed)
        throw_error("There was a structure binded to this thread.\n");

    // Bind this
    std_set_tls_data(m_thread_tls_id, this);

    // Create a domain for this thread
    char buf[64];
    snprintf(buf, sizeof(buf), "Thread%lld", (Int64)std_get_current_task_id());
    buf[sizeof(buf) - 1] = 0;
    m_start_domain = XNEW(Domain, buf);
    switch_domain(m_start_domain);

    // Create a context for this thread
    m_start_context = XNEW(CallContextNode, this);
    m_context = m_start_context;

    // Join this context into start domain
    m_start_domain->m_context_list.append_node(m_start_context);
}

// Thread will be stopped
void Thread::stop()
{
    // Verify
    auto existed = (Thread *)std_get_tls_data(m_thread_tls_id);
    if (existed != this)
        throw_error("This structure isn't binded to this thread.\n");

    if (m_value_list.get_count())
    {
        printf("There are still %zu active reference values when thread is stopped.\n",
               m_value_list.get_count());
        m_value_list.free();
    }

    // Destroy start domain & context for convenience
    STD_ASSERT(m_current_domain == m_start_domain);
    STD_ASSERT(m_context == m_start_context);

    // Remove context from the start domain
    m_start_domain->m_context_list.remove_node(m_start_context);
    XDELETE(m_start_context);
    m_start_context = 0;
    m_context = 0;

    m_start_domain->leave();
    XDELETE(m_start_domain);

    // Unbind
    std_set_tls_data(m_thread_tls_id, 0);
}

// Update stack information of start context
void Thread::update_start_sp_of_start_context(void *start_sp)
{
    // Alignment to 4K
    start_sp = (void *)(((size_t)start_sp | 0xFFF) + 1);
    m_start_context->value.m_start_sp = start_sp;
}

// Enter function call
void Thread::enter_function_call(CallContextNode *context)
{
    // Put this context in domain
    if (m_current_domain)
        m_current_domain->m_context_list.append_node(context);
}

// Leave function call
// Pop stack & restore function context
Value Thread::leave_function_call(CallContextNode *context, Value& ret)
{
    STD_ASSERT(("There must be existed current_domain.", m_current_domain));
    m_current_domain->m_context_list.remove_node(context);

    auto c = &context->value;

    // Get previous domain object & back to previous frame
    Domain *prev_domain = c->m_prev_context->value.m_domain;
    c->m_thread->m_context = c->m_prev_context;

    if (!will_change_domain(prev_domain))
        // Domain won't be changed, return "ret" directly
        return ret;

    // Domain will be changed, try to copy ret to target domain
    ret.copy_to_local(c->m_thread);
    c->m_thread->switch_domain(prev_domain);
    transfer_values_to_current_domain();
    return ret;
}

// Switch execution ownership to a new domain
void Thread::switch_domain(Domain *to_domain)
{
    if (m_current_domain == to_domain)
        // No target domain or domain is not switched
        return;

    if (m_current_domain)
        // Leave previous domain
        m_current_domain->leave();

    if (to_domain)
        // Enter new domain
        to_domain->enter();

    m_current_domain = to_domain;
}

// Switch execution ownership to a new domain
void Thread::switch_object(Object *to_object)
{
    Domain *to_domain = to_object ? to_object->get_domain() : 0;
    switch_domain(to_domain);
}

// Switch object by oid
// Since the object may be destructed in other thread, I must lock the
// object & switch to it
bool Thread::try_switch_object_by_id(Thread *thread, ObjectId to_oid, Value *args, ArgNo n)
{
    // ATTENTION:
    // We can these values no matter the entry is freed or allocated, since
    // the memory of entry won't be return to memory pool.
    // HOW EVER, these valus may be changed by other thread during the following
    // operation
    auto *entry = Object::get_entry_by_id(to_oid);
    auto *to_domain = entry->domain;

    if (m_current_domain != to_domain)
    {
        // Domain will be changed
        // Copy arguments to thread local value list & pass to target
        for (ArgNo i = 0; i < n; i++)
            args[i].copy_to_local(thread);

        // OK, Switch the domain first
        if (m_current_domain)
            // Leave previous domain
            m_current_domain->leave();

        if (to_domain)
            // Enter new domain
            to_domain->enter();
    }

    // ATTENTION:
    // Now, after entered the "to_domain". I can verify the values since the those
    // won't be changed if they are belonged to "to_domain"
    auto *ob = entry->object;
    if (ob && ob->get_oid() == to_oid && ob->get_domain() == to_domain)
    {
        // OK, switch to right domain
        m_current_domain = to_domain;
        thread->transfer_values_to_current_domain();
        return true;
    }

    // Switch to wrong domain
    STD_TRACE("! Switch interrupt, ob: %p(%llx), expect: %llx.\n",
              ob, ob->get_oid().i64, to_oid.i64);

    // Rollback, return to previous domain
    if (to_domain)
        // Leave new domain
        to_domain->leave();

    if (m_current_domain)
        // Enter previous domain
        m_current_domain->enter();

    // Transfer the value no matter the domain to avoid them left on thread
    thread->transfer_values_to_current_domain();
    return false;
}

// Should I change domain if enter target object?
bool Thread::will_change_domain(Domain *to_domain)
{
    if (m_current_domain == to_domain)
        // No target domain or domain is not switched
        return false;

    // I will change domain
    return true;
}

// Return context of this thread
// ({
//     ([
//         "name" : <Domain_name>
//         "id" : <Domain_id>
//         ...
//         "stack_top" : <Top address of stack frame>
//         "stack_bottom" : <Bottom address of stack frame>
//     ])
//     ...
// })
Value Thread::get_context_list()
{
    // Get count of context list
    size_t count = 0;
    auto *p = m_context;
    while (p)
    {
        count++;
        p = p->prev;
    }

    // Update the end_sp
    update_end_sp_of_current_context();

    // Allocate array for return
    Array arr(count);
    p = m_context;
    while (p)
    {
        Map map = p->value.m_domain->get_domain_detail();
        map["stack_top"] = (size_t)p->value.m_start_sp;
        map["stack_bottom"] = (size_t)p->value.m_end_sp;
        arr.push_back(map);
        p = p->prev;
    }

    return arr;    
}

// Transfer all values in local value list to current domain
// This routine should be invoked after having switched to a domain
void Thread::transfer_values_to_current_domain()
{
    if (m_value_list.get_count() && m_current_domain)
        m_current_domain->concat_value_list(&m_value_list);
}

// Throw exception
void throw_error(const char *msg, ...)
{
    // Create formatted string
    va_list va;
    va_start(va, msg);
    char buf[1024];
    STD_VSNPRINTF(buf, sizeof(buf), msg, va);
    buf[sizeof(buf) - 1] = 0;
    va_end(va);

    // Throw the message
    throw buf;
}

} // End of namespace: cmm
