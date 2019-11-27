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

#define ERR_MSG_LEN           1024

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

typedef struct aajson_val
{
	enum AAJSON_VALUE_TYPE type;
	union aajson_val_data {
		char str[AAJSON_STR_MAX_SIZE];
		int int_num;
		double dbl_num;
	} data;
	size_t str_len;
} aajson_val;

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
	char *s;
	size_t col, line;

	int error;
	int end;
	char errmsg[ERR_MSG_LEN];

	aajson_callback callback;
	void *user;

	aajson_val val;

	size_t path_stack_pos;
	struct aajson_path_item path_stack[AAJSON_STACK_DEPTH];
};

#define CHECK_END(I)                    \
do {                                    \
	if (*(I->s) == '\0') {          \
		I->end = 1;             \
		return;                 \
	}                               \
} while (0)

/* unexpected end of input */
#define CHECK_END_UNEXP(I, ERR)         \
do {                                    \
	if (*(I->s) == '\0') {          \
		I->end = 1;             \
		I->error = 1;           \
		strcpy(I->errmsg, ERR); \
		return;                 \
	}                               \
} while (0)

#define AAJSON_EXPECT_SYM_IN_KW(I, C)                                      \
do {                                                                       \
	I->s++;                                                            \
	CHECK_END_UNEXP(I, "unexpected end of input inside keyword");      \
	if (*(I->s) != C) {                                                \
		I->error = 1;                                              \
		sprintf(I->errmsg, "expected '%c', got '%c'", C, *(I->s)); \
		return;                                                    \
	}                                                                  \
	I->col++;                                                          \
} while (0)

static void aajson_object(struct aajson *i);
static void aajson_value(struct aajson *i);

/* comments and whitespace */
static void
aajson_c_style_comment(struct aajson *i)
{
	for (;;) {
		i->s++;
		CHECK_END_UNEXP(i,
			"unexpected end of input inside the comment");

		i->col++;
		if (*(i->s) == '*') {
			/* end of comment? */
			i->s++;
			CHECK_END_UNEXP(i,
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
		CHECK_END(i);

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
		CHECK_END(i);
		if ((*(i->s) == ' ') || (*(i->s) == '\t')) {
			i->col++;
		} else if ((*(i->s) == '\n') || (*(i->s) == '\r')) {
			i->line++;
			i->col = 1;
		} else if (*(i->s) == '/') {
			/* comment? */
			i->s++;

			CHECK_END_UNEXP(i,
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
	CHECK_END_UNEXP(i,
		"unexpected end of input");

	aajson_whitespace(i);
	if (i->end || i->error) return;

	CHECK_END_UNEXP(i,
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
			CHECK_END_UNEXP(i,
				"Unexpected end of input inside the string");

			if ((*(i->s) >= '0') && (*(i->s) <= '9')) {
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
		CHECK_END_UNEXP(i,
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

			CHECK_END_UNEXP(i,
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

/* array */
static void
aajson_array(struct aajson *i)
{
	aajson_whitespace(i);
	if (i->end || i->error) return;

	CHECK_END_UNEXP(i,
		"unexpected end of input inside array");

	if (*(i->s) == ']') {
		/* end of empty array */
		i->s++;
	} else {
		/* array items */
		for (;;) {
			aajson_value(i);
			if (i->end || i->error) return;
			CHECK_END_UNEXP(i, "unexpected end of input");

			aajson_whitespace(i);
			if (i->end || i->error) return;

			if (*(i->s) == ']') {
				/* end of array */
				i->s++;
				break;
			} else if (*(i->s) == ',') {
				/* next value */
				i->s++;
				CHECK_END_UNEXP(i,
					"unexpected end of input after ','");
				i->col++;
				aajson_whitespace(i);
				if (i->end || i->error) return;

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
	CHECK_END_UNEXP(i, "unexpected end of input");

	aajson_whitespace(i);
	if (i->end || i->error) return;

	CHECK_END_UNEXP(i, "unexpected end of input");

	if (*(i->s) == '\"') {
		/* string */
		i->s++;
		CHECK_END_UNEXP(i,
			"unexpected end of input inside the string");
		i->col++;

		aajson_string(i, i->val.data.str, &i->val.str_len);
		i->val.type = AAJSON_VALUE_STRING;

		/* user supplied callback */
		if (!((i->callback)(i, &i->val, i->user))) {
			i->error = 1;
			return;
		}

	} else if ((*(i->s) >= '0') && (*(i->s) <= '9')) {
		/* number */
	} else if (*(i->s) == '{') {
		/* object */
		i->s++;
		CHECK_END_UNEXP(i,
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
		CHECK_END_UNEXP(i,
			"unexpected end of input inside array");
		i->col++;

		i->path_stack_pos++;
		i->path_stack[i->path_stack_pos].type =
			AAJSON_PATH_ITEM_STRING;

		aajson_array(i);

		i->path_stack_pos--;

	} else if (*(i->s) == 't') {
		/* true */
		AAJSON_EXPECT_SYM_IN_KW(i, 'r');
		AAJSON_EXPECT_SYM_IN_KW(i, 'u');
		AAJSON_EXPECT_SYM_IN_KW(i, 'e');

		i->val.type = AAJSON_VALUE_TRUE;
		i->s++;
		i->col++;
	} else if (*(i->s) == 'f') {
		/* false */
		AAJSON_EXPECT_SYM_IN_KW(i, 'a');
		AAJSON_EXPECT_SYM_IN_KW(i, 'l');
		AAJSON_EXPECT_SYM_IN_KW(i, 's');
		AAJSON_EXPECT_SYM_IN_KW(i, 'e');

		i->val.type = AAJSON_VALUE_FALSE;
		i->s++;
		i->col++;
	} else if (*(i->s) == 'n') {
		/* null */
		AAJSON_EXPECT_SYM_IN_KW(i, 'u');
		AAJSON_EXPECT_SYM_IN_KW(i, 'l');
		AAJSON_EXPECT_SYM_IN_KW(i, 'l');

		i->val.type = AAJSON_VALUE_NULL;
		i->s++;
		i->col++;
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

	CHECK_END_UNEXP(i,
		"unexpected end of input inside object");

	if (*(i->s) == '}') {
		/* end of empty object */
		i->s++;
	} else if (*(i->s) == '\"') {
		/* set of key-value pairs */
		for (;;) {
			/* key */
			i->s++;
			CHECK_END_UNEXP(i,
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
			CHECK_END_UNEXP(i,
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
				CHECK_END_UNEXP(i,
					"unexpected end of input after ','");
				i->col++;
				aajson_whitespace(i);
				if (i->end || i->error) return;
				CHECK_END_UNEXP(i,
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

static void
aajson_init(struct aajson *i, char *data)
{
	memset(i, 0, sizeof(struct aajson));
	i->s = data;
	i->col = 1;
}

static void
aajson_parse(struct aajson *i, aajson_callback callback, void *user)
{
	i->callback = callback;
	i->user = user;

	aajson_value(i);
}

#endif

