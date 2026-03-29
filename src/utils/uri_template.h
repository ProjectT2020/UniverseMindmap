#ifndef UM_URI_TEMPLATE_H
#define UM_URI_TEMPLATE_H

#include <stddef.h>

typedef struct {
	const char *name;
	const char *value;
} UriTemplateVar;

/*
 * Expand RFC 6570 level 1 expressions in template_str.
 *
 * Supported expression form for level 1 in this implementation:
 *   {var}
 *
 * Return value:
 *   0  -> success
 *  -1  -> invalid arguments, malformed template, unsupported expression,
 *         or output buffer too small.
 */
int uri_template_expand(const char *template_str,
						const UriTemplateVar *vars,
						size_t var_count,
						char *output,
						size_t output_size);

#endif /* UM_URI_TEMPLATE_H */
