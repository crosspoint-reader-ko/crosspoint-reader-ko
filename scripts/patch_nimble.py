"""Make NimBLE's host stop survive repeated deinit/init cycles.

NimBLE-Arduino 2.5.1 keeps the stop procedure's listener list and connection count in
statics that a host init never clears. A stop that did not complete leaves nimble_port_stop's
static listener on the list; the next stop inserts the same node again, the list points at
itself, ble_hs_stop_done calls the release callback until the counting semaphore is full,
and npl_freertos_sem_release asserts (ret == pdPASS). The radio is stopped and started on
every chapter build that runs short of heap, so a book could hit this on each section.

The patch: registering a listener that is already on the list is a no-op, a host init starts
with an empty list and a zero count, and every stop recounts its connections (as upstream
mynewt-nimble b9035d28 does). ble_hs_stop_dup_registrations counts skipped duplicates.
"""

from pathlib import Path

Import("env")


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
MARKER = "/* CrossPoint: stop listener guard */"

REPLACEMENTS = [
    (
        "static struct ble_npl_callout ble_hs_stop_terminate_tmo;\n",
        "static struct ble_npl_callout ble_hs_stop_terminate_tmo;\n"
        f"{MARKER}\n"
        "volatile uint32_t ble_hs_stop_dup_registrations;\n",
    ),
    (
        "    BLE_HS_DBG_ASSERT(fn != NULL);\n\n"
        "    listener->fn = fn;\n",
        "    BLE_HS_DBG_ASSERT(fn != NULL);\n\n"
        "    struct ble_hs_stop_listener *cur;\n"
        "    SLIST_FOREACH(cur, &ble_hs_stop_listeners, link) {\n"
        "        if (cur == listener) {\n"
        "            ble_hs_stop_dup_registrations++;\n"
        "            listener->fn = fn;\n"
        "            listener->arg = arg;\n"
        "            return;\n"
        "        }\n"
        "    }\n\n"
        "    listener->fn = fn;\n",
    ),
    (
        "    ble_hs_lock();\n"
        "    ble_hs_conn_foreach(ble_hs_stop_terminate_conn, NULL);\n",
        "    ble_hs_stop_conn_cnt = 0;\n\n"
        "    ble_hs_lock();\n"
        "    ble_hs_conn_foreach(ble_hs_stop_terminate_conn, NULL);\n",
    ),
    (
        "ble_hs_stop_init(void)\n{\n",
        "ble_hs_stop_init(void)\n{\n"
        "    SLIST_INIT(&ble_hs_stop_listeners);\n"
        "    ble_hs_stop_conn_cnt = 0;\n",
    ),
]


def patch_stop(path: Path) -> None:
    text = path.read_text()
    if MARKER in text:
        return
    for old, new in REPLACEMENTS:
        if text.count(old) != 1:
            raise SystemExit(f"patch_nimble: unexpected source in {path} (looking for {old.splitlines()[0]!r})")
        text = text.replace(old, new)
    path.write_text(text)
    print(f"Patched NimBLE host stop: {path.relative_to(PROJECT_DIR)}")


for stop_c in PROJECT_DIR.glob(".pio/libdeps/*/NimBLE-Arduino/src/nimble/nimble/host/src/ble_hs_stop.c"):
    patch_stop(stop_c)
