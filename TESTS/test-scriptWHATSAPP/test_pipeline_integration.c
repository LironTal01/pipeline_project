/* ------------------------------------------------------------------ *
 *  End-to-end: run analyzer in a child process                       *
 * ------------------------------------------------------------------ */
#include <check.h>
#include <stdio.h>
#include <string.h>

START_TEST(e2e_upper_logger)
        {
                FILE *fp = popen("cd ../../ && ./output/analyzer 8 uppercaser logger", "w");
        ck_assert_ptr_nonnull(fp);

        fputs("hello\n<END>\n", fp);
        pclose(fp);                      /* closes stdin, analyzer drains */

        /* collect analyzer output */
        FILE *out = popen("cd ../../ && echo 'hello\n<END>' | ./output/analyzer 8 uppercaser logger", "r");
        if (!out) { ck_abort_msg("missing log output"); }

        char buf[64]; fgets(buf, sizeof buf, out);
        pclose(out);

        ck_assert_msg(strstr(buf, "HELLO"), "expected transformed text");
        }
END_TEST

        Suite *pipeline_suite(void)
{
    Suite *s = suite_create("pipeline");
    TCase *tc = tcase_create("core");
    tcase_add_test(tc, e2e_upper_logger);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    SRunner *sr = srunner_create(pipeline_suite());
    srunner_run_all(sr, CK_ENV);
    int nf = srunner_ntests_failed(sr);
    srunner_free(sr);
    return nf ? 1 : 0;
}
