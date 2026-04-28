#include <node_api.h>
#include <stdlib.h>
#include <string.h>
#include "bm.h"

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

#define NAPI_CALL(env, call)                                          \
    do {                                                              \
        napi_status _s = (call);                                      \
        if (_s != napi_ok) {                                          \
            const napi_extended_error_info *info;                     \
            napi_get_last_error_info((env), &info);                   \
            napi_throw_error((env), NULL,                             \
                info->error_message ? info->error_message             \
                                    : "N-API call failed");           \
            return NULL;                                              \
        }                                                             \
    } while (0)

/* Get buffer data from a Buffer or string argument */
static napi_status get_needle_data(napi_env env, napi_value val,
                                   uint8_t **data, size_t *len,
                                   uint8_t **alloc_buf)
{
    napi_valuetype vtype;
    bool is_buf;
    napi_status s;

    *alloc_buf = NULL;

    s = napi_typeof(env, val, &vtype);
    if (s != napi_ok) return s;

    s = napi_is_buffer(env, val, &is_buf);
    if (s != napi_ok) return s;

    if (is_buf) {
        void *buf_data;
        s = napi_get_buffer_info(env, val, &buf_data, len);
        if (s != napi_ok) return s;
        *data = (uint8_t *)buf_data;
        return napi_ok;
    }

    if (vtype == napi_string) {
        size_t str_len;
        s = napi_get_value_string_utf8(env, val, NULL, 0, &str_len);
        if (s != napi_ok) return s;
        if (str_len >= SIZE_MAX) return napi_generic_failure;
        *alloc_buf = (uint8_t *)malloc(str_len + 1);
        if (!*alloc_buf) return napi_generic_failure;
        s = napi_get_value_string_utf8(env, val, (char *)*alloc_buf,
                                       str_len + 1, &str_len);
        if (s != napi_ok) { free(*alloc_buf); *alloc_buf = NULL; return s; }
        *data = *alloc_buf;
        *len = str_len;
        return napi_ok;
    }

    napi_throw_type_error(env, NULL, "needle must be a Buffer or string");
    return napi_invalid_arg;
}

/* Extract search options from the options object */
static void parse_options(napi_env env, napi_value opts,
                          int *overlap, int64_t *offset,
                          int64_t *limit)
{
    napi_valuetype vtype;
    napi_value val;
    bool bval;

    *overlap = 0;
    *offset = 0;
    *limit = -1; /* -1 = no limit */

    if (!opts) return;
    if (napi_typeof(env, opts, &vtype) != napi_ok) return;
    if (vtype != napi_object) return;

    /* overlap */
    if (napi_get_named_property(env, opts, "overlap", &val) == napi_ok) {
        if (napi_typeof(env, val, &vtype) == napi_ok && vtype == napi_boolean) {
            napi_get_value_bool(env, val, &bval);
            *overlap = bval ? 1 : 0;
        }
    }

    /* offset */
    if (napi_get_named_property(env, opts, "offset", &val) == napi_ok) {
        if (napi_typeof(env, val, &vtype) == napi_ok && vtype == napi_number) {
            int64_t v;
            napi_get_value_int64(env, val, &v);
            if (v > 0) *offset = v;
        }
    }

    /* limit */
    if (napi_get_named_property(env, opts, "limit", &val) == napi_ok) {
        if (napi_typeof(env, val, &vtype) == napi_ok && vtype == napi_number) {
            double d;
            napi_get_value_double(env, val, &d);
            /* Infinity check */
            if (d > 0 && d < (double)SIZE_MAX)
                *limit = (int64_t)d;
            /* else stays -1 = unlimited */
        }
    }
}

/* Build a JS array of int64 offsets */
static napi_value make_result_array(napi_env env, int64_t *results,
                                    size_t count)
{
    napi_value arr, val;
    size_t i;

    if (napi_create_array_with_length(env, count, &arr) != napi_ok)
        return NULL;

    for (i = 0; i < count; i++) {
        napi_create_int64(env, results[i], &val);
        napi_set_element(env, arr, (uint32_t)i, val);
    }
    return arr;
}

/* ------------------------------------------------------------------ */
/*  search(haystack, needle, options?)                                  */
/* ------------------------------------------------------------------ */

static napi_value js_search(napi_env env, napi_callback_info info)
{
    size_t argc = 3;
    napi_value argv[3];
    napi_value result;
    bool is_buf;
    uint8_t *haystack_data, *needle_data, *needle_alloc = NULL;
    size_t haystack_len, needle_len;
    int overlap;
    int64_t offset, limit;
    bm_context_t *ctx;
    int64_t *results_buf = NULL;
    size_t max_results, match_count;

    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));

    if (argc < 2) {
        napi_throw_type_error(env, NULL,
            "search requires at least 2 arguments: haystack, needle");
        return NULL;
    }

    /* haystack must be Buffer */
    NAPI_CALL(env, napi_is_buffer(env, argv[0], &is_buf));
    if (!is_buf) {
        napi_throw_type_error(env, NULL, "haystack must be a Buffer");
        return NULL;
    }
    NAPI_CALL(env, napi_get_buffer_info(env, argv[0],
                                        (void **)&haystack_data,
                                        &haystack_len));

    /* needle */
    if (get_needle_data(env, argv[1], &needle_data, &needle_len,
                        &needle_alloc) != napi_ok)
        return NULL;

    if (needle_len == 0) {
        if (needle_alloc) free(needle_alloc);
        napi_throw_range_error(env, NULL, "needle must not be empty");
        return NULL;
    }

    /* options */
    parse_options(env, argc >= 3 ? argv[2] : NULL, &overlap, &offset, &limit);

    /* compile */
    ctx = bm_compile(needle_data, needle_len);
    if (!ctx) {
        if (needle_alloc) free(needle_alloc);
        napi_throw_error(env, NULL, "Failed to compile search pattern");
        return NULL;
    }

    /* allocate results buffer */
    max_results = (limit > 0) ? (size_t)limit : (haystack_len + 1);
    /* Reasonable cap to avoid huge allocations */
    if (max_results > haystack_len + 1)
        max_results = haystack_len + 1;
    /* Guard against multiplication overflow */
    if (max_results > SIZE_MAX / sizeof(int64_t)) {
        bm_free(ctx);
        if (needle_alloc) free(needle_alloc);
        napi_throw_range_error(env, NULL, "haystack too large");
        return NULL;
    }

    results_buf = (int64_t *)malloc(max_results * sizeof(int64_t));
    if (!results_buf) {
        bm_free(ctx);
        if (needle_alloc) free(needle_alloc);
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }

    match_count = bm_search(ctx, haystack_data, haystack_len,
                            (size_t)offset, overlap,
                            results_buf, max_results);

    result = make_result_array(env, results_buf, match_count);

    free(results_buf);
    bm_free(ctx);
    if (needle_alloc) free(needle_alloc);

    return result;
}

/* ------------------------------------------------------------------ */
/*  count(haystack, needle, options?)                                   */
/* ------------------------------------------------------------------ */

static napi_value js_count(napi_env env, napi_callback_info info)
{
    size_t argc = 3;
    napi_value argv[3];
    napi_value result;
    bool is_buf;
    uint8_t *haystack_data, *needle_data, *needle_alloc = NULL;
    size_t haystack_len, needle_len;
    int overlap;
    int64_t offset, limit;
    bm_context_t *ctx;
    size_t match_count;

    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));

    if (argc < 2) {
        napi_throw_type_error(env, NULL,
            "count requires at least 2 arguments: haystack, needle");
        return NULL;
    }

    NAPI_CALL(env, napi_is_buffer(env, argv[0], &is_buf));
    if (!is_buf) {
        napi_throw_type_error(env, NULL, "haystack must be a Buffer");
        return NULL;
    }
    NAPI_CALL(env, napi_get_buffer_info(env, argv[0],
                                        (void **)&haystack_data,
                                        &haystack_len));

    if (get_needle_data(env, argv[1], &needle_data, &needle_len,
                        &needle_alloc) != napi_ok)
        return NULL;

    if (needle_len == 0) {
        if (needle_alloc) free(needle_alloc);
        napi_throw_range_error(env, NULL, "needle must not be empty");
        return NULL;
    }

    parse_options(env, argc >= 3 ? argv[2] : NULL, &overlap, &offset, &limit);
    (void)limit; /* count ignores limit */

    ctx = bm_compile(needle_data, needle_len);
    if (!ctx) {
        if (needle_alloc) free(needle_alloc);
        napi_throw_error(env, NULL, "Failed to compile search pattern");
        return NULL;
    }

    match_count = bm_count(ctx, haystack_data, haystack_len,
                           (size_t)offset, overlap);

    bm_free(ctx);
    if (needle_alloc) free(needle_alloc);

    NAPI_CALL(env, napi_create_int64(env, (int64_t)match_count, &result));
    return result;
}

/* ------------------------------------------------------------------ */
/*  searchAsync(haystack, needle, options?) → Promise                   */
/* ------------------------------------------------------------------ */

typedef struct {
    napi_async_work work;
    napi_deferred deferred;
    napi_ref haystack_ref;
    napi_ref needle_ref;
    uint8_t *haystack_data;
    size_t haystack_len;
    uint8_t *needle_data;
    size_t needle_len;
    uint8_t *needle_alloc; /* non-NULL if we allocated for string */
    int overlap;
    int64_t offset;
    int64_t limit;
    /* results */
    int64_t *results;
    size_t result_count;
    char *error;
} async_data_t;

static void async_execute(napi_env env, void *data)
{
    async_data_t *d = (async_data_t *)data;
    bm_context_t *ctx;
    size_t max_results;

    (void)env;

    ctx = bm_compile(d->needle_data, d->needle_len);
    if (!ctx) {
        d->error = "Failed to compile search pattern";
        return;
    }

    max_results = (d->limit > 0) ? (size_t)d->limit
                                 : (d->haystack_len + 1);
    if (max_results > d->haystack_len + 1)
        max_results = d->haystack_len + 1;
    if (max_results > SIZE_MAX / sizeof(int64_t)) {
        bm_free(ctx);
        d->error = "Result buffer too large";
        return;
    }

    d->results = (int64_t *)malloc(max_results * sizeof(int64_t));
    if (!d->results) {
        bm_free(ctx);
        d->error = "Out of memory";
        return;
    }

    d->result_count = bm_search(ctx, d->haystack_data, d->haystack_len,
                                (size_t)d->offset, d->overlap,
                                d->results, max_results);
    bm_free(ctx);
}

static void async_complete(napi_env env, napi_status status, void *data)
{
    async_data_t *d = (async_data_t *)data;
    napi_value result;

    if (status == napi_cancelled || d->error) {
        napi_value err;
        napi_create_string_utf8(env, d->error ? d->error : "Cancelled",
                                NAPI_AUTO_LENGTH, &err);
        napi_reject_deferred(env, d->deferred, err);
    } else {
        result = make_result_array(env, d->results, d->result_count);
        if (result)
            napi_resolve_deferred(env, d->deferred, result);
        else {
            napi_value err;
            napi_create_string_utf8(env, "Failed to create result array",
                                    NAPI_AUTO_LENGTH, &err);
            napi_reject_deferred(env, d->deferred, err);
        }
    }

    /* Cleanup */
    napi_delete_reference(env, d->haystack_ref);
    napi_delete_reference(env, d->needle_ref);
    napi_delete_async_work(env, d->work);
    if (d->results) free(d->results);
    if (d->needle_alloc) free(d->needle_alloc);
    free(d);
}

static napi_value js_search_async(napi_env env, napi_callback_info info)
{
    size_t argc = 3;
    napi_value argv[3];
    napi_value promise, resource_name;
    bool is_buf;
    async_data_t *d;

    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));

    if (argc < 2) {
        napi_throw_type_error(env, NULL,
            "searchAsync requires at least 2 arguments: haystack, needle");
        return NULL;
    }

    NAPI_CALL(env, napi_is_buffer(env, argv[0], &is_buf));
    if (!is_buf) {
        napi_throw_type_error(env, NULL, "haystack must be a Buffer");
        return NULL;
    }

    d = (async_data_t *)calloc(1, sizeof(async_data_t));
    if (!d) {
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }

    /* haystack */
    NAPI_CALL(env, napi_get_buffer_info(env, argv[0],
                                        (void **)&d->haystack_data,
                                        &d->haystack_len));
    NAPI_CALL(env, napi_create_reference(env, argv[0], 1, &d->haystack_ref));

    /* needle */
    if (get_needle_data(env, argv[1], &d->needle_data, &d->needle_len,
                        &d->needle_alloc) != napi_ok) {
        napi_delete_reference(env, d->haystack_ref);
        free(d);
        return NULL;
    }

    if (d->needle_len == 0) {
        napi_delete_reference(env, d->haystack_ref);
        if (d->needle_alloc) free(d->needle_alloc);
        free(d);
        napi_throw_range_error(env, NULL, "needle must not be empty");
        return NULL;
    }

    /* Keep a reference to needle to prevent GC */
    if (napi_create_reference(env, argv[1], 1, &d->needle_ref) != napi_ok) {
        napi_delete_reference(env, d->haystack_ref);
        if (d->needle_alloc) free(d->needle_alloc);
        free(d);
        napi_throw_error(env, NULL, "Failed to create needle reference");
        return NULL;
    }

    /* options */
    parse_options(env, argc >= 3 ? argv[2] : NULL,
                  &d->overlap, &d->offset, &d->limit);

    /* Guard against multiplication overflow in async worker */
    {
        size_t async_max = (d->limit > 0) ? (size_t)d->limit
                                           : (d->haystack_len + 1);
        if (async_max > d->haystack_len + 1)
            async_max = d->haystack_len + 1;
        if (async_max > SIZE_MAX / sizeof(int64_t)) {
            napi_delete_reference(env, d->haystack_ref);
            napi_delete_reference(env, d->needle_ref);
            if (d->needle_alloc) free(d->needle_alloc);
            free(d);
            napi_throw_range_error(env, NULL, "haystack too large");
            return NULL;
        }
    }

    /* Create promise */
    if (napi_create_promise(env, &d->deferred, &promise) != napi_ok) {
        napi_delete_reference(env, d->haystack_ref);
        napi_delete_reference(env, d->needle_ref);
        if (d->needle_alloc) free(d->needle_alloc);
        free(d);
        napi_throw_error(env, NULL, "Failed to create promise");
        return NULL;
    }

    /* Create and queue async work */
    if (napi_create_string_utf8(env, "grep:searchAsync",
                                NAPI_AUTO_LENGTH, &resource_name) != napi_ok ||
        napi_create_async_work(env, NULL, resource_name,
                               async_execute, async_complete,
                               d, &d->work) != napi_ok ||
        napi_queue_async_work(env, d->work) != napi_ok)
    {
        napi_delete_reference(env, d->haystack_ref);
        napi_delete_reference(env, d->needle_ref);
        if (d->needle_alloc) free(d->needle_alloc);
        free(d);
        napi_throw_error(env, NULL, "Failed to queue async work");
        return NULL;
    }

    return promise;
}

/* ------------------------------------------------------------------ */
/*  Module init                                                         */
/* ------------------------------------------------------------------ */

static napi_value init(napi_env env, napi_value exports)
{
    napi_value fn;

    napi_create_function(env, "search", NAPI_AUTO_LENGTH,
                         js_search, NULL, &fn);
    napi_set_named_property(env, exports, "search", fn);

    napi_create_function(env, "count", NAPI_AUTO_LENGTH,
                         js_count, NULL, &fn);
    napi_set_named_property(env, exports, "count", fn);

    napi_create_function(env, "searchAsync", NAPI_AUTO_LENGTH,
                         js_search_async, NULL, &fn);
    napi_set_named_property(env, exports, "searchAsync", fn);

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
