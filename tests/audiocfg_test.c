#define _POSIX_C_SOURCE 200809L

#include <check.h>

#include "../src/lib.h"

START_TEST(test_acfg_match_name) {
    ck_assert(acfg_match_name("alsa_output.pci", NULL, "alsa_output.pci"));
    ck_assert(acfg_match_name("x", "Built-in Audio", "Built-in Audio"));
    ck_assert(!acfg_match_name("a", "b", "c"));
}
END_TEST

START_TEST(test_acfg_parse_device_spec) {
    int unified = 0;
    int kind = -1;
    unsigned card = 0;
    const char *name = NULL;

    ck_assert_int_eq(acfg_parse_device_spec("3", &unified, &kind, &card, &name), 0);
    ck_assert_int_eq(unified, 3);
    ck_assert_int_eq(kind, -1);

    ck_assert_int_eq(acfg_parse_device_spec("playback:48", &unified, &kind, &card, &name), 0);
    ck_assert_int_eq(kind, 0);
    ck_assert_uint_eq(card, 48);

    ck_assert_int_eq(acfg_parse_device_spec("capture:50", &unified, &kind, &card, &name), 0);
    ck_assert_int_eq(kind, 1);
    ck_assert_uint_eq(card, 50);

    ck_assert_int_eq(acfg_parse_device_spec(":50", &unified, &kind, &card, &name), 0);
    ck_assert_int_eq(kind, -1);
    ck_assert_uint_eq(card, 50);

    ck_assert_int_eq(acfg_parse_device_spec("alsa_output.foo", &unified, &kind, &card, &name), 0);
    ck_assert_ptr_eq(name, "alsa_output.foo");
}
END_TEST

static Suite *audiocfg_suite(void) {
    Suite *s = suite_create("audiocfg");
    TCase *tc_core = tcase_create("core");

    tcase_add_test(tc_core, test_acfg_match_name);
    tcase_add_test(tc_core, test_acfg_parse_device_spec);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void) {
    Suite *s = audiocfg_suite();
    SRunner *sr = srunner_create(s);
    int failed;

    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
