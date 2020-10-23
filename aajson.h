/*
 * aajson
 *
 * Copyright (c) 2019, Vladimir Misyurov
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef aajson_h_included
#define aajson_h_included


#include <stdio.h>
#include <string.h>

#define AAJSON_ERR_MSG_LEN    512

#ifndef AAJSON_STR_MAX_SIZE
#define AAJSON_STR_MAX_SIZE   512
#endif

#ifndef AAJSON_STACK_DEPTH
#define AAJSON_STACK_DEPTH    32
#endif

typedef struct aajson_val aajson_val;
struct aajson;

typedef int (*aajson_callback)
	(struct aajson *a, aajson_val *value, void *user);

enum AAJSON_VALUE_TYPE
{
	AAJSON_VALUE_STRING,
	AAJSON_VALUE_NUM,
	AAJSON_VALUE_TRUE,
	AAJSON_VALUE_FALSE,
	AAJSON_VALUE_NULL
};

enum AAJSON_PATH_ITEM_TYPE
{
	AAJSON_PATH_ITEM_STRING,
	AAJSON_PATH_ITEM_ARRAY
};

enum AAJSON_PATH_MATCH_TYPE
{
	AAJSON_PATH_MATCH_STRING,
	AAJSON_PATH_MATCH_ARRAY,
	AAJSON_PATH_MATCH_ANY_ITEM,
	AAJSON_PATH_MATCH_ANY
};

struct aajson_val
{
	enum AAJSON_VALUE_TYPE type;

	char str[AAJSON_STR_MAX_SIZE];
	size_t str_len;
};

struct aajson_path_item
{
	enum AAJSON_PATH_ITEM_TYPE type;
	union aajson_path_item_data {
		char path_item[AAJSON_STR_MAX_SIZE];
		size_t array_idx;
	} data;
	size_t str_len;
};

struct aajson
{
	const char *s;
	size_t col, line;

	int error;
	int end;
	char errmsg[AAJSON_ERR_MSG_LEN];

	aajson_callback callback;
	void *user;

	aajson_val val;

	size_t path_stack_pos;
	struct aajson_path_item path_stack[AAJSON_STACK_DEPTH];
};

struct aajson_path_match_item
{
	enum AAJSON_PATH_MATCH_TYPE type;
	char str[AAJSON_STR_MAX_SIZE];
};

struct aajson_path_matches
{
	size_t size;
	struct aajson_path_match_item items[AAJSON_STACK_DEPTH];
};

#define AAJSON_CHECK_END(I)             \
do {                                    \
	if (*(I->s) == '\0') {          \
		I->end = 1;             \
		return;                 \
	}                               \
} while (0)

/* unexpected end of input */
#define AAJSON_CHECK_END_UNEXP(I, ERR)  \
do {                                    \
	if (*(I->s) == '\0') {          \
		I->end = 1;             \
		I->error = 1;           \
		strcpy(I->errmsg, ERR); \
		return;                 \
	}                               \
} while (0)

#define AAJSON_EXPECT_SYM_IN_KW(I, C)                                       \
do {                                                                        \
	I->s++;                                                             \
	AAJSON_CHECK_END_UNEXP(I, "unexpected end of input inside keyword");\
	if (*(I->s) != C) {                                                 \
		I->error = 1;                                               \
		sprintf(I->errmsg, "expected '%c', got '%c'", C, *(I->s));  \
		return;                                                     \
	}                                                                   \
	I->col++;                                                           \
	I->val.str[I->val.str_len] = C;                                     \
	I->val.str_len++;                                                   \
} while (0) 

static void aajson_object(struct aajson *i);
static void aajson_value(struct aajson *i);

static int
aajson_is_digit(int c)
{
	return ((c >= '0') && (c <= '9'));
}

/* comments and whitespace */
static void
aajson_c_style_comment(struct aajson *i)
{
	for (;;) {
		i->s++;
		AAJSON_CHECK_END_UNEXP(i,
			"unexpected end of input inside the comment");

		i->col++;
		if (*(i->s) == '*') {
			/* end of comment? */
			i->s++;
			AAJSON_CHECK_END_UNEXP(i,
				"unexpected end of input inside the comment");

			i->col++;
			if (*(i->s) == '/') {
				/* end of comment */
				break;
			}
		} else if (*(i->s) == '\n') {
			i->line++;
			i->col = 1;
		}
	}
}

static void
aajson_one_line_comment(struct aajson *i)
{
	for (;;) {
		i->s++;
		AAJSON_CHECK_END(i);

		i->col++;
		if (*(i->s) == '\n') {
			/* end of comment */
			i->line++;
			i->col = 1;

			break;
		}
	}
}

static void
aajson_whitespace(struct aajson *i)
{
	for (;;) {
		AAJSON_CHECK_END(i);
		if ((*(i->s) == ' ') || (*(i->s) == '\t')) {
			i->col++;
		} else if ((*(i->s) == '\n') || (*(i->s) == '\r')) {
			i->line++;
			i->col = 1;
		} else if (*(i->s) == '/') {
			/* comment? */
			i->s++;

			AAJSON_CHECK_END_UNEXP(i,
				"unexpected end of input after '/'");

			i->col++;
			if (*(i->s) == '*') {
				aajson_c_style_comment(i);
				if (i->end || i->error) return;
			} else if (*(i->s) == '/') {
				aajson_one_line_comment(i);
				if (i->end || i->error) return;
			} else {
				/* unknown token, ungetc() and return */
				i->col--;
				i->s--;
				break;
			}
		} else {
			/* end of whitespace */
			break;
		}
		i->s++;
	}
}

/* expect one symbol */
static void
aajson_symbol(struct aajson *i, int c)
{
	AAJSON_CHECK_END_UNEXP(i,
		"unexpected end of input");

	aajson_whitespace(i);
	if (i->end || i->error) return;

	AAJSON_CHECK_END_UNEXP(i,
		"unexpected end of input");

	if (*(i->s) == c) {
		i->col++;
	} else {
		i->error = 1;
		sprintf(i->errmsg, "expected '%c', got symbol '%c'",
			c, *(i->s));
	}
	i->s++;
}


/* strings */
static void
aajson_string_append(struct aajson *i, char *str, size_t *len, int c)
{
	str[*len] = c;
	(*len)++;
	if (*len + 1 >= AAJSON_STR_MAX_SIZE) {
		i->error = 1;
		strcpy(i->errmsg, "String too long");
	}
}

static void
aajson_escaped_symbol(struct aajson *i, char *str, size_t *len)
{
	if ((*(i->s) == '\"') || (*(i->s) == '\\') || (*(i->s) == '/')) {
		i->col++;
		aajson_string_append(i, str, len, *(i->s));
	} else if (*(i->s) == 'b') {
		i->col++;
		aajson_string_append(i, str, len, '\b');
	} else if (*(i->s) == 'f') {
		i->col++;
		aajson_string_append(i, str, len, '\f');
	} else if (*(i->s) == 'n') {
		i->col++;
		aajson_string_append(i, str, len, '\n');
	} else if (*(i->s) == 'r') {
		i->col++;
		aajson_string_append(i, str, len, '\r');
	} else if (*(i->s) == 't') {
		i->col++;
		aajson_string_append(i, str, len, '\t');
	} else if (*(i->s) == 'u') {
		int j;
		unsigned char symbol[4];
		/* 4 hex digit */
		i->col++;

		for (j=0; j<4; j++) {
			i->s++;
			AAJSON_CHECK_END_UNEXP(i,
				"Unexpected end of input inside the string");

			if (aajson_is_digit(*(i->s))) {
				symbol[j] = *(i->s) - '0';
				i->col++;
			} else if ((*(i->s) >= 'a') && (*(i->s) <= 'f')) {
				symbol[j] = (*(i->s) - 'a') + 10;
				i->col++;
			} else if ((*(i->s) >= 'A') && (*(i->s) <= 'F')) {
				symbol[j] = (*(i->s) - 'A') + 10;
				i->col++;
			} else {
				i->error = 1;
				sprintf(i->errmsg,
					"Unknown symbol '%c' in digit",
					*(i->s));
				return;
			}
		}
		/* FIXME: is it correct? */
		aajson_string_append(i, str, len, symbol[2] * 16 + symbol[3]);
		aajson_string_append(i, str, len, symbol[0] * 16 + symbol[1]);
	}
}

static void
aajson_string(struct aajson *i, char *str, size_t *len)
{
	*len = 0;
	for (;;) {
		AAJSON_CHECK_END_UNEXP(i,
			"Unexpected end of input inside the string");

		i->col++;
		if (*(i->s) == '\"') {
			/* end of string */
			i->col++;

			/* skip to next symbol */
			i->s++;
			break;
		} else if (*(i->s) == '\\') {
			/* next symbol */
			i->s++;

			AAJSON_CHECK_END_UNEXP(i,
				"Unexpected end of input inside the string");

			i->col++;
			aajson_escaped_symbol(i, str, len);
			if (i->end || i->error) return;

		} else if ((*(i->s) == '\n') || (*(i->s) == '\r')) {
			i->error = 1;
			strcpy(i->errmsg, "CR/LF are not allowed in strings");
			break;
		} else {
			aajson_string_append(i, str, len, *(i->s));
			if (i->end || i->error) return;

			i->col++;
		}

		i->s++;
	}

	/* append terminating zero */
	str[*len] = '\0';

}

/* number */
static void
aajson_number(struct aajson *i)
{
	i->val.str_len = 0;
	i->val.str[i->val.str_len] = *(i->s);
	i->val.str_len++;

	i->s++;
	AAJSON_CHECK_END(i);
	i->col++;

	while (aajson_is_digit(*(i->s))) {
		i->val.str[i->val.str_len] = *(i->s);
		i->val.str_len++;

		i->s++;
		AAJSON_CHECK_END(i);
		i->col++;
	}
	i->val.str[i->val.str_len] = '\0';

	if (*(i->s) == '.') {
		/* fraction */
		i->val.str[i->val.str_len] = *(i->s);
		i->val.str_len++;

		i->s++;
		AAJSON_CHECK_END_UNEXP(i,
			"unexpected end of input inside number");
		i->col++;

		if (!aajson_is_digit(*(i->s))) {
			i->error = 1;
			sprintf(i->errmsg, "expected digit, got symbol '%c'",
				*(i->s));
			return;
		}

		while (aajson_is_digit(*(i->s))) {
			i->val.str[i->val.str_len] = *(i->s);
			i->val.str_len++;

			i->s++;
			AAJSON_CHECK_END(i);
			i->col++;
		}
	} else {
		/* not a digit or . */
		return;
	}
	i->val.str[i->val.str_len] = '\0';

	if ((*(i->s) == 'e') || (*(i->s) == 'E')) {
		/* exponent */
		i->val.str[i->val.str_len] = *(i->s);
		i->val.str_len++;

		i->s++;
		AAJSON_CHECK_END_UNEXP(i,
			"unexpected end of input inside number");
		i->col++;

		if ((*(i->s) == '-') || (*(i->s) == '+')) {
			i->val.str[i->val.str_len] = *(i->s);
			i->val.str_len++;

			i->s++;
			AAJSON_CHECK_END_UNEXP(i,
				"unexpected end of input inside number");
			i->col++;

			if (!aajson_is_digit(*(i->s))) {
				i->error = 1;
				sprintf(i->errmsg,
					"expected digit, got symbol '%c'",
					*(i->s));
				return;
			}

		} else if (!aajson_is_digit(*(i->s))) {
			i->error = 1;
			sprintf(i->errmsg, "expected digit, got symbol '%c'",
				*(i->s));
			return;
		}

		/* read exponent */
		while (aajson_is_digit(*(i->s))) {
			i->val.str[i->val.str_len] = *(i->s);
			i->val.str_len++;

			i->s++;
			AAJSON_CHECK_END(i);
			i->col++;
		}
	} else {
		return;
	}
	i->val.str[i->val.str_len] = '\0';
}

/* array */
static void
aajson_array(struct aajson *i)
{
	aajson_whitespace(i);
	if (i->end || i->error) return;

	AAJSON_CHECK_END_UNEXP(i,
		"unexpected end of input inside array");

	if (*(i->s) == ']') {
		/* end of empty array */
		i->s++;
	} else {
		/* array items */
		for (;;) {
			aajson_value(i);
			if (i->end || i->error) return;
			AAJSON_CHECK_END_UNEXP(i, "unexpected end of input");

			aajson_whitespace(i);
			if (i->end || i->error) return;

			if (*(i->s) == ']') {
				/* end of array */
				i->s++;
				break;
			} else if (*(i->s) == ',') {
				/* next value */
				i->s++;
				AAJSON_CHECK_END_UNEXP(i,
					"unexpected end of input after ','");
				i->col++;
				aajson_whitespace(i);
				if (i->end || i->error) return;

				i->path_stack[i->path_stack_pos]
					.data.array_idx += 1;

				continue;
			} else {
				i->error = 1;
				sprintf(i->errmsg,
					"expected ']' or ',', got symbol '%c'",
					*(i->s));
				return;
			}
		}
	}
}

/* value */
static void
aajson_value(struct aajson *i)
{
	AAJSON_CHECK_END_UNEXP(i, "unexpected end of input");

	aajson_whitespace(i);
	if (i->end || i->error) return;

	AAJSON_CHECK_END_UNEXP(i, "unexpected end of input");

	if (*(i->s) == '\"') {
		/* string */
		i->s++;
		AAJSON_CHECK_END_UNEXP(i,
			"unexpected end of input inside the string");
		i->col++;

		aajson_string(i, i->val.str, &i->val.str_len);
		if (i->end || i->error) return;

		i->val.type = AAJSON_VALUE_STRING;

		/* user supplied callback */
		if (!((i->callback)(i, &i->val, i->user))) {
			i->error = 1;
			return;
		}

	} else if (aajson_is_digit(*(i->s))) {
		/* number */
		aajson_number(i);
		if (i->end || i->error) return;

		i->val.type = AAJSON_VALUE_NUM;

		if (!((i->callback)(i, &i->val, i->user))) {
			i->error = 1;
			return;
		}

	} else if (*(i->s) == '-') {
		/* negative number */

	} else if (*(i->s) == '{') {
		/* object */
		i->s++;
		AAJSON_CHECK_END_UNEXP(i,
			"unexpected end of input inside object");
		i->col++;

		i->path_stack_pos++;
		i->path_stack[i->path_stack_pos].type =
			AAJSON_PATH_ITEM_STRING;

		aajson_object(i);

		i->path_stack_pos--;

	} else if (*(i->s) == '[') {
		/* array */
		i->s++;
		AAJSON_CHECK_END_UNEXP(i,
			"unexpected end of input inside array");
		i->col++;

		i->path_stack_pos++;
		i->path_stack[i->path_stack_pos].type =
			AAJSON_PATH_ITEM_ARRAY;

		i->path_stack[i->path_stack_pos].data.array_idx = 0;

		aajson_array(i);

		i->path_stack_pos--;

	} else if (*(i->s) == 't') {
		/* true */
		i->val.str_len = 0;
		i->val.str[i->val.str_len++] = *(i->s);

		AAJSON_EXPECT_SYM_IN_KW(i, 'r');
		AAJSON_EXPECT_SYM_IN_KW(i, 'u');
		AAJSON_EXPECT_SYM_IN_KW(i, 'e');
		i->val.str[i->val.str_len] = '\0';

		i->val.type = AAJSON_VALUE_TRUE;
		i->s++;
		i->col++;

		if (!((i->callback)(i, &i->val, i->user))) {
			i->error = 1;
			return;
		}
	} else if (*(i->s) == 'f') {
		/* false */
		i->val.str_len = 0;
		i->val.str[i->val.str_len++] = *(i->s);

		AAJSON_EXPECT_SYM_IN_KW(i, 'a');
		AAJSON_EXPECT_SYM_IN_KW(i, 'l');
		AAJSON_EXPECT_SYM_IN_KW(i, 's');
		AAJSON_EXPECT_SYM_IN_KW(i, 'e');

		i->val.type = AAJSON_VALUE_FALSE;
		i->s++;
		i->col++;

		if (!((i->callback)(i, &i->val, i->user))) {
			i->error = 1;
			return;
		}
	} else if (*(i->s) == 'n') {
		/* null */
		i->val.str_len = 0;
		i->val.str[i->val.str_len++] = *(i->s);

		AAJSON_EXPECT_SYM_IN_KW(i, 'u');
		AAJSON_EXPECT_SYM_IN_KW(i, 'l');
		AAJSON_EXPECT_SYM_IN_KW(i, 'l');
		i->val.str[i->val.str_len] = '\0';

		i->val.type = AAJSON_VALUE_NULL;
		i->s++;
		i->col++;

		if (!((i->callback)(i, &i->val, i->user))) {
			i->error = 1;
			return;
		}
	} else {
		i->error = 1;
		sprintf(i->errmsg, "unexpected symbol '%c'", *(i->s));
	}
}

/* object */
static void
aajson_object(struct aajson *i)
{
	aajson_whitespace(i);
	if (i->end || i->error) return;

	AAJSON_CHECK_END_UNEXP(i,
		"unexpected end of input inside object");

	if (*(i->s) == '}') {
		/* end of empty object */
		i->s++;
	} else if (*(i->s) == '\"') {
		/* set of key-value pairs */
		for (;;) {
			/* key */
			i->s++;
			AAJSON_CHECK_END_UNEXP(i,
				"unexpected end of input inside the string");
			i->col++;

			aajson_string(i,
				i->path_stack[i->path_stack_pos].data.path_item,
				&i->path_stack[i->path_stack_pos].str_len);
			if (i->end || i->error) return;

			aajson_symbol(i, ':');
			if (i->end || i->error) return;

			aajson_value(i);
			if (i->end || i->error) return;

			/* optional whitespace */
			AAJSON_CHECK_END_UNEXP(i,
				"unexpected end of input inside object");

			aajson_whitespace(i);
			if (i->end || i->error) return;

			if (*(i->s) == '}') {
				/* end of object */
				i->s++;
				break;
			} else if (*(i->s) == ',') {
				/* next key-value pair */
				i->s++;
				AAJSON_CHECK_END_UNEXP(i,
					"unexpected end of input after ','");
				i->col++;
				aajson_whitespace(i);
				if (i->end || i->error) return;
				AAJSON_CHECK_END_UNEXP(i,
					"unexpected end of input after ','");

				if (*(i->s) == '\"') {
					continue;
				} else {
					i->error = 1;
					sprintf(i->errmsg,
						"expected '\"', "
						"got symbol '%c'",
						*(i->s));
					return;
				}
			} else {
				i->error = 1;
				sprintf(i->errmsg,
					"expected '}' or ',', got symbol '%c'",
					*(i->s));
				return;
			}
		}
	} else {
		/* unknown token */
		i->error = 1;
		sprintf(i->errmsg,
			"expected end of object or key, got symbol '%c'",
			*(i->s));
	}
}

static inline void
aajson_init(struct aajson *i, const char *data)
{
	memset(i, 0, sizeof(struct aajson));
	i->s = data;
	i->line = i->col = 1;
}

static inline void
aajson_parse(struct aajson *i, aajson_callback callback, void *user)
{
	i->callback = callback;
	i->user = user;

	aajson_value(i);
}

/* path matching */
static void
aajson_match_string(const char *path)
{
	(void)path;
}

static inline int
aajson_match(struct aajson *i, const char *path)
{
	const char *s = path;
	(void)i;

	/* initial checks */
	if (*s == '\0') {
		return 0;
	} else if (*s != '$') {
		return 0;
	}

	/* first symbol */
	s++;
	if (*s == '\0') {
		return 0;
	} else if (*s == '.') {
		s++;
		if (*s == '\0') {
			/* end of input */
		} else if (*s == '.') {
			/* any */
		} else {
			/* */
		}
	} else if (*s == '*') {
		/* any */
	} else {
		/* string to match */
		aajson_match_string(s);
	}

	return 1;
}

#endif

