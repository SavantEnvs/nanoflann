// Leak detection is off by default for every ASan target (SPEC §6.2 item 15); ASan and UBSan stay on.
extern "C" int __lsan_is_turned_off() { return 1; }
