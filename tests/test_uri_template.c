#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/utils/uri_template.h"

static void test_level1_examples(void) {
    UriTemplateVar vars[] = {
        { "var", "value" },
        { "hello", "Hello World!" },
    };
    char out[256];

    assert(uri_template_expand("{var}", vars, 2, out, sizeof(out)) == 0);
    assert(strcmp(out, "value") == 0);

    assert(uri_template_expand("{hello}", vars, 2, out, sizeof(out)) == 0);
    assert(strcmp(out, "Hello%20World%21") == 0);
}

static void test_literal_and_missing_var(void) {
    UriTemplateVar vars[] = {
        { "var", "value" },
    };
    char out[256];

    assert(uri_template_expand("prefix-{var}-suffix", vars, 1, out, sizeof(out)) == 0);
    assert(strcmp(out, "prefix-value-suffix") == 0);

    assert(uri_template_expand("before-{missing}-after", vars, 1, out, sizeof(out)) == 0);
    assert(strcmp(out, "before--after") == 0);
}

static void test_invalid_templates(void) {
    UriTemplateVar vars[] = {
        { "var", "value" },
    };
    char out[32];

    assert(uri_template_expand("{", vars, 1, out, sizeof(out)) == -1);
    assert(uri_template_expand("}", vars, 1, out, sizeof(out)) == -1);
    assert(uri_template_expand("{}", vars, 1, out, sizeof(out)) == -1);
    assert(uri_template_expand("{+var}", vars, 1, out, sizeof(out)) == -1);
}

int main(void) {
    test_level1_examples();
    test_literal_and_missing_var();
    test_invalid_templates();

    printf("[PASS] uri_template tests\n");
    return 0;
}