/*
 * testharness.js — minimal subset of WPT's harness, sufficient for
 * the script-test corpus (dom/, html/dom/, css/cssom-* slices). The
 * full upstream file is ~3000 LOC and pulls in DOM features we don't
 * model; this is the slice our runner exercises.
 *
 * What we implement:
 *
 *   test(fn, name)              -- synchronous test
 *   async_test(name | fn, name) -- step / step_func / done semantics
 *   promise_test(fn, name)      -- async fn returning a Promise
 *
 *   assert_true / assert_false
 *   assert_equals / assert_not_equals
 *   assert_array_equals
 *   assert_object_equals
 *   assert_throws_js / assert_throws_dom
 *   assert_unreached / assert_implements / assert_in_array
 *   assert_regexp_match
 *
 *   setup(opts)                 -- timeout / explicit_done
 *   add_completion_callback(cb)
 *   done()
 *
 * Results land on globalThis.__ylexbor_test_results__ as an array of
 * { name, status, message }.  status ∈ {"PASS","FAIL","TIMEOUT","NOTRUN"}.
 *
 * The runner reads that array after the document boot pump and emits
 * one JSON-Line per entry to stdout. Aggregate pass/fail is computed
 * by the runner, not here.
 */
(function (globalThis) {
    "use strict";

    const STATUS = { PASS: 0, FAIL: 1, TIMEOUT: 2, NOTRUN: 3 };
    const STATUS_NAME = ["PASS", "FAIL", "TIMEOUT", "NOTRUN"];

    const results = [];
    globalThis.__ylexbor_test_results__ = results;

    let pending_async = 0;
    let completion_callbacks = [];
    let explicit_done = false;
    let suite_done = false;

    function _fire_completion() {
        if (suite_done) return;
        if (pending_async > 0) return;
        suite_done = true;
        for (const cb of completion_callbacks) {
            try { cb(results); } catch (_) {}
        }
    }

    function _record(name, status, message) {
        results.push({
            name: String(name || "<anonymous>"),
            status: STATUS_NAME[status],
            message: message ? String(message) : "",
        });
    }

    /* ---------------------------------------------------------------
     * Sync test()
     * -------------------------------------------------------------*/
    globalThis.test = function (fn, name) {
        try {
            fn.call({ name });
            _record(name, STATUS.PASS, "");
        } catch (e) {
            _record(name, STATUS.FAIL, _format_error(e));
        }
    };

    /* ---------------------------------------------------------------
     * async_test() — returns a Test object with step/step_func/done.
     * Tests stay PENDING until done() is called or step_func throws.
     * -------------------------------------------------------------*/
    globalThis.async_test = function (fn_or_name, name_opt) {
        const name = typeof fn_or_name === "string" ? fn_or_name : name_opt;
        const fn   = typeof fn_or_name === "function" ? fn_or_name : null;
        let idx = results.length;
        results.push({ name: String(name || "<async>"), status: "NOTRUN", message: "" });
        pending_async++;
        let finished = false;

        const obj = {
            name,
            step(f /*, ...args*/) {
                if (finished) return;
                try { return f.apply(this, Array.prototype.slice.call(arguments, 1)); }
                catch (e) { obj._fail(e); }
            },
            step_func(f) {
                return function () {
                    return obj.step(f, ...arguments);
                };
            },
            step_func_done(f) {
                return function () {
                    if (f) obj.step(f, ...arguments);
                    obj.done();
                };
            },
            unreached_func(msg) {
                return function () { obj._fail(new Error(msg || "unreached")); };
            },
            done() {
                if (finished) return;
                finished = true;
                if (results[idx].status === "NOTRUN") {
                    results[idx].status = "PASS";
                }
                pending_async--;
                if (pending_async === 0 && explicit_done) _fire_completion();
            },
            _fail(e) {
                if (finished) return;
                finished = true;
                results[idx].status = "FAIL";
                results[idx].message = _format_error(e);
                pending_async--;
                if (pending_async === 0 && explicit_done) _fire_completion();
            },
        };

        if (fn) {
            try { fn.call(obj, obj); }
            catch (e) { obj._fail(e); }
        }
        return obj;
    };

    /* ---------------------------------------------------------------
     * promise_test()
     * -------------------------------------------------------------*/
    globalThis.promise_test = function (fn, name) {
        let idx = results.length;
        results.push({ name: String(name || "<promise>"), status: "NOTRUN", message: "" });
        pending_async++;
        Promise.resolve()
            .then(() => fn({ name }))
            .then(() => {
                results[idx].status = "PASS";
                pending_async--;
                if (pending_async === 0 && explicit_done) _fire_completion();
            })
            .catch(e => {
                results[idx].status = "FAIL";
                results[idx].message = _format_error(e);
                pending_async--;
                if (pending_async === 0 && explicit_done) _fire_completion();
            });
    };

    /* ---------------------------------------------------------------
     * assert_*
     * -------------------------------------------------------------*/
    function _stringify(v) {
        try {
            if (v === undefined) return "undefined";
            if (v === null)      return "null";
            if (typeof v === "string") return JSON.stringify(v);
            if (typeof v === "number" || typeof v === "boolean") return String(v);
            if (typeof v === "function") return "function " + (v.name || "");
            return JSON.stringify(v);
        } catch (_) { return String(v); }
    }

    function _format_error(e) {
        if (!e) return "";
        if (e.stack) return e.stack;
        return String(e);
    }

    globalThis.assert_true = function (v, msg) {
        if (v !== true) throw new Error("assert_true: expected true got " + _stringify(v) + (msg ? ": " + msg : ""));
    };
    globalThis.assert_false = function (v, msg) {
        if (v !== false) throw new Error("assert_false: expected false got " + _stringify(v) + (msg ? ": " + msg : ""));
    };
    globalThis.assert_equals = function (a, b, msg) {
        if (a !== b && !(a !== a && b !== b)) {
            throw new Error("assert_equals: expected " + _stringify(b) + " got " + _stringify(a) + (msg ? ": " + msg : ""));
        }
    };
    globalThis.assert_not_equals = function (a, b, msg) {
        if (a === b || (a !== a && b !== b)) {
            throw new Error("assert_not_equals: " + _stringify(a) + " === " + _stringify(b) + (msg ? ": " + msg : ""));
        }
    };
    globalThis.assert_array_equals = function (a, b, msg) {
        if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) {
            throw new Error("assert_array_equals: length mismatch" + (msg ? ": " + msg : ""));
        }
        for (let i = 0; i < a.length; i++) {
            if (a[i] !== b[i]) {
                throw new Error("assert_array_equals: index " + i + " " + _stringify(a[i]) + " !== " + _stringify(b[i]) + (msg ? ": " + msg : ""));
            }
        }
    };
    globalThis.assert_object_equals = function (a, b, msg) {
        const ka = Object.keys(a || {});
        const kb = Object.keys(b || {});
        if (ka.length !== kb.length) throw new Error("assert_object_equals: key count differs" + (msg ? ": " + msg : ""));
        for (const k of ka) {
            if (a[k] !== b[k]) throw new Error("assert_object_equals: key " + JSON.stringify(k) + (msg ? ": " + msg : ""));
        }
    };
    globalThis.assert_throws_js = function (ctor, fn, msg) {
        try { fn(); } catch (e) { if (ctor && !(e instanceof ctor)) {
            throw new Error("assert_throws_js: wrong exception type" + (msg ? ": " + msg : ""));
        } return; }
        throw new Error("assert_throws_js: did not throw" + (msg ? ": " + msg : ""));
    };
    globalThis.assert_throws_dom = function (name, fn, msg) {
        try { fn(); } catch (e) { return; }
        throw new Error("assert_throws_dom(" + name + "): did not throw" + (msg ? ": " + msg : ""));
    };
    globalThis.assert_unreached = function (msg) {
        throw new Error("assert_unreached" + (msg ? ": " + msg : ""));
    };
    globalThis.assert_implements = function (cond, msg) {
        if (!cond) throw new Error("assert_implements" + (msg ? ": " + msg : ""));
    };
    globalThis.assert_in_array = function (v, arr, msg) {
        if (!Array.isArray(arr) || arr.indexOf(v) < 0) {
            throw new Error("assert_in_array: " + _stringify(v) + " not in array" + (msg ? ": " + msg : ""));
        }
    };
    globalThis.assert_regexp_match = function (s, re, msg) {
        if (!(re instanceof RegExp) || !re.test(String(s))) {
            throw new Error("assert_regexp_match" + (msg ? ": " + msg : ""));
        }
    };
    globalThis.assert_class_string = function (v, name, msg) {
        const got = Object.prototype.toString.call(v);
        const want = "[object " + name + "]";
        if (got !== want) {
            throw new Error("assert_class_string: expected " + want + " got " + got + (msg ? ": " + msg : ""));
        }
    };

    /* ---------------------------------------------------------------
     * Lifecycle
     * -------------------------------------------------------------*/
    globalThis.setup = function (opts) {
        if (opts && opts.explicit_done) explicit_done = true;
    };
    globalThis.done = function () {
        if (pending_async === 0) _fire_completion();
    };
    globalThis.add_completion_callback = function (cb) {
        if (typeof cb === "function") completion_callbacks.push(cb);
        if (suite_done) cb(results);
    };
    /* WPT's testharnessreport.js hook — we don't dispatch to a parent
     * frame, the runner reads results out of __ylexbor_test_results__. */
    globalThis.add_result_callback = function () {};
    globalThis.add_start_callback  = function () {};
    /* Format helpers some tests reach for. */
    globalThis.format_value = _stringify;

    /* Completion is driven by the runner: after all scripts have
     * executed and the boot-budget pump is exhausted, the runner
     * calls __ylexbor_finalize_tests__() which fires the registered
     * completion callbacks (if any) and locks the result array.
     *
     * We do NOT auto-fire on first microtask drain — that would
     * complete BEFORE the page's own inline test() calls even ran,
     * leaving the results array empty. */
    globalThis.__ylexbor_finalize_tests__ = function () {
        _fire_completion();
        return results.length;
    };
})(globalThis);
