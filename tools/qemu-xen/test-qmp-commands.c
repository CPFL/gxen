#include <glib.h>
#include "qemu-objects.h"
#include "test-qmp-commands.h"
#include "qapi/qmp-core.h"
#include "module.h"

void qmp_user_def_cmd(Error **errp)
{
}

void qmp_user_def_cmd1(UserDefOne * ud1, Error **errp)
{
}

UserDefTwo * qmp_user_def_cmd2(UserDefOne * ud1a, UserDefOne * ud1b, Error **errp)
{
    UserDefTwo *ret;
    UserDefOne *ud1c = g_malloc0(sizeof(UserDefOne));
    UserDefOne *ud1d = g_malloc0(sizeof(UserDefOne));

    ud1c->string = strdup(ud1a->string);
    ud1c->integer = ud1a->integer;
    ud1d->string = strdup(ud1b->string);
    ud1d->integer = ud1b->integer;

    ret = g_malloc0(sizeof(UserDefTwo));
    ret->string = strdup("blah1");
    ret->dict.string = strdup("blah2");
    ret->dict.dict.userdef = ud1c;
    ret->dict.dict.string = strdup("blah3");
    ret->dict.has_dict2 = true;
    ret->dict.dict2.userdef = ud1d;
    ret->dict.dict2.string = strdup("blah4");

    return ret;
}

/* test commands with no input and no return value */
static void test_dispatch_cmd(void)
{
    QDict *req = qdict_new();
    QObject *resp;

    qdict_put_obj(req, "execute", QOBJECT(qstring_from_str("user_def_cmd")));

    resp = qmp_dispatch(QOBJECT(req));
    assert(resp != NULL);
    assert(!qdict_haskey(qobject_to_qdict(resp), "error"));
    g_print("\nresp: %s\n", qstring_get_str(qobject_to_json(resp)));

    qobject_decref(resp);
    QDECREF(req);
}

/* test commands that return an error due to invalid parameters */
static void test_dispatch_cmd_error(void)
{
    QDict *req = qdict_new();
    QObject *resp;

    qdict_put_obj(req, "execute", QOBJECT(qstring_from_str("user_def_cmd2")));

    resp = qmp_dispatch(QOBJECT(req));
    assert(resp != NULL);
    assert(qdict_haskey(qobject_to_qdict(resp), "error"));
    g_print("\nresp: %s\n", qstring_get_str(qobject_to_json_pretty(resp)));

    qobject_decref(resp);
    QDECREF(req);
}

/* test commands that involve both input parameters and return values */
static void test_dispatch_cmd_io(void)
{
    QDict *req = qdict_new();
    QDict *args = qdict_new();
    QDict *ud1a = qdict_new();
    QDict *ud1b = qdict_new();
    QObject *resp;

    qdict_put_obj(ud1a, "integer", QOBJECT(qint_from_int(42)));
    qdict_put_obj(ud1a, "string", QOBJECT(qstring_from_str("hello")));
    qdict_put_obj(ud1b, "integer", QOBJECT(qint_from_int(422)));
    qdict_put_obj(ud1b, "string", QOBJECT(qstring_from_str("hello2")));
    qdict_put_obj(args, "ud1a", QOBJECT(ud1a));
    qdict_put_obj(args, "ud1b", QOBJECT(ud1b));
    qdict_put_obj(req, "arguments", QOBJECT(args));

    qdict_put_obj(req, "execute", QOBJECT(qstring_from_str("user_def_cmd2")));

    /* TODO: put in full payload and check for errors */
    resp = qmp_dispatch(QOBJECT(req));
    assert(resp != NULL);
    assert(!qdict_haskey(qobject_to_qdict(resp), "error"));
    g_print("\nresp: %s\n", qstring_get_str(qobject_to_json_pretty(resp)));

    qobject_decref(resp);
    QDECREF(req);
}

/* test generated dealloc functions for generated types */
static void test_dealloc_types(void)
{
    UserDefOne *ud1test, *ud1a, *ud1b;
    UserDefOneList *ud1list;

    ud1test = g_malloc0(sizeof(UserDefOne));
    ud1test->integer = 42;
    ud1test->string = g_strdup("hi there 42");

    qapi_free_UserDefOne(ud1test);

    ud1a = g_malloc0(sizeof(UserDefOne));
    ud1a->integer = 43;
    ud1a->string = g_strdup("hi there 43");

    ud1b = g_malloc0(sizeof(UserDefOne));
    ud1b->integer = 44;
    ud1b->string = g_strdup("hi there 44");

    ud1list = g_malloc0(sizeof(UserDefOneList));
    ud1list->value = ud1a;
    ud1list->next = g_malloc0(sizeof(UserDefOneList));
    ud1list->next->value = ud1b;

    qapi_free_UserDefOneList(ud1list);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/0.15/dispatch_cmd", test_dispatch_cmd);
    g_test_add_func("/0.15/dispatch_cmd_error", test_dispatch_cmd_error);
    g_test_add_func("/0.15/dispatch_cmd_io", test_dispatch_cmd_io);
    g_test_add_func("/0.15/dealloc_types", test_dealloc_types);

    module_call_init(MODULE_INIT_QAPI);
    g_test_run();

    return 0;
}
