#include "uri_template.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static int append_char(char *output, size_t output_size, size_t *out_len, char ch) {
	if (*out_len + 1 >= output_size) {
		return -1;
	}
	output[*out_len] = ch;
	(*out_len)++;
	return 0;
}

static int append_percent_encoded(char *output,
								  size_t output_size,
								  size_t *out_len,
								  unsigned char ch) {
	static const char HEX[] = "0123456789ABCDEF";
	if (*out_len + 3 >= output_size) {
		return -1;
	}
	output[*out_len] = '%';
	output[*out_len + 1] = HEX[(ch >> 4) & 0x0F];
	output[*out_len + 2] = HEX[ch & 0x0F];
	*out_len += 3;
	return 0;
}

static int is_unreserved(unsigned char ch) {
	return isalnum(ch) || ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

static const char *find_var_value(const char *name,
								  size_t name_len,
								  const UriTemplateVar *vars,
								  size_t var_count) {
	size_t i;
	for (i = 0; i < var_count; i++) {
		if (vars[i].name == NULL) {
			continue;
		}
		if (strlen(vars[i].name) == name_len && strncmp(vars[i].name, name, name_len) == 0) {
			return vars[i].value;
		}
	}
	return NULL;
}

static int append_level1_var(char *output,
							 size_t output_size,
							 size_t *out_len,
							 const char *name,
							 size_t name_len,
							 const UriTemplateVar *vars,
							 size_t var_count) {
	const char *value;
	size_t i;

	value = find_var_value(name, name_len, vars, var_count);
	if (value == NULL) {
		return 0;
	}

	for (i = 0; value[i] != '\0'; i++) {
		unsigned char ch = (unsigned char)value[i];
		if (is_unreserved(ch)) {
			if (append_char(output, output_size, out_len, (char)ch) != 0) {
				return -1;
			}
		} else {
			if (append_percent_encoded(output, output_size, out_len, ch) != 0) {
				return -1;
			}
		}
	}

	return 0;
}

int uri_template_expand(const char *template_str,
						const UriTemplateVar *vars,
						size_t var_count,
						char *output,
						size_t output_size) {
	size_t i;
	size_t out_len = 0;

	if (template_str == NULL || output == NULL || output_size == 0) {
		return -1;
	}

	for (i = 0; template_str[i] != '\0'; i++) {
		if (template_str[i] == '{') {
			size_t start = i + 1;
			size_t end = start;

			while (template_str[end] != '\0' && template_str[end] != '}') {
				end++;
			}

			if (template_str[end] != '}') {
				return -1;
			}
			if (end == start) {
				return -1;
			}

			/* level 1 does not support operator-based expressions */
			if (strchr("+#./;?&", template_str[start]) != NULL) {
				return -1;
			}

			if (append_level1_var(output,
								  output_size,
								  &out_len,
								  &template_str[start],
								  end - start,
								  vars,
								  var_count) != 0) {
				return -1;
			}

			i = end;
			continue;
		}

		if (template_str[i] == '}') {
			return -1;
		}

		if (append_char(output, output_size, &out_len, template_str[i]) != 0) {
			return -1;
		}
	}

	output[out_len] = '\0';
	return 0;
}
