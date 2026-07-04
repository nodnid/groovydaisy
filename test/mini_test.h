#pragma once
// Minimal host-side test harness: named tests, non-aborting checks,
// non-zero exit on any failure. No dependencies.
#include <cstdio>

namespace minitest
{

struct Test
{
    const char* name;
    void (*fn)();
    Test* next;
};

inline Test*& List()
{
    static Test* head = nullptr;
    return head;
}

inline int& Failures()
{
    static int f = 0;
    return f;
}

struct Register
{
    Register(Test* t)
    {
        t->next = List();
        List()  = t;
    }
};

inline int RunAll()
{
    // Registration order is LIFO; reverse so output follows file order.
    Test* reversed = nullptr;
    for(Test* t = List(); t;)
    {
        Test* next = t->next;
        t->next    = reversed;
        reversed   = t;
        t          = next;
    }

    int count = 0;
    for(Test* t = reversed; t; t = t->next)
    {
        int before = Failures();
        t->fn();
        std::printf("[%s] %s\n", Failures() == before ? "PASS" : "FAIL", t->name);
        count++;
    }
    std::printf("%d tests, %d failed checks\n", count, Failures());
    return Failures() ? 1 : 0;
}

} // namespace minitest

#define TEST(name)                                                        \
    static void test_##name();                                            \
    static minitest::Test test_obj_##name{#name, test_##name, nullptr};   \
    static minitest::Register test_reg_##name{&test_obj_##name};          \
    static void test_##name()

#define CHECK(expr)                                                       \
    do                                                                    \
    {                                                                     \
        if(!(expr))                                                       \
        {                                                                 \
            std::printf("  CHECK failed: %s (%s:%d)\n", #expr, __FILE__,  \
                        __LINE__);                                        \
            ++minitest::Failures();                                       \
        }                                                                 \
    } while(0)

#define CHECK_EQ(a, b)                                                    \
    do                                                                    \
    {                                                                     \
        long long va = (long long)(a);                                    \
        long long vb = (long long)(b);                                    \
        if(va != vb)                                                      \
        {                                                                 \
            std::printf("  CHECK_EQ failed: %s == %s (got %lld vs %lld) " \
                        "(%s:%d)\n",                                      \
                        #a, #b, va, vb, __FILE__, __LINE__);              \
            ++minitest::Failures();                                       \
        }                                                                 \
    } while(0)

#define CHECK_NEAR(a, b, tol)                                             \
    do                                                                    \
    {                                                                     \
        double va = (double)(a);                                          \
        double vb = (double)(b);                                          \
        double d  = va > vb ? va - vb : vb - va;                          \
        if(d > (tol))                                                     \
        {                                                                 \
            std::printf("  CHECK_NEAR failed: %s ~= %s (got %f vs %f, "   \
                        "tol %f) (%s:%d)\n",                              \
                        #a, #b, va, vb, (double)(tol), __FILE__,          \
                        __LINE__);                                        \
            ++minitest::Failures();                                       \
        }                                                                 \
    } while(0)
