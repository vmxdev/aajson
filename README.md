# aajson

aajson is a simple JSON streaming parser written in C

## Documentation
(to be written)

## Example in C

```c
#include <stdio.h>

#include "aajson.h"


static int
callback(struct aajson *a, aajson_val *value, void *user)
{
	(void)user;

	printf("path pos: %lu, path: '%s', type id: %d ", a->path_stack_pos,
		a->path_stack[a->path_stack_pos].data.path_item,
		a->path_stack[a->path_stack_pos].type);
	printf("value: '%s' (%d)\n", value->str, value->type);

	return 1;
}


int
main()
{
	char json[] = 
		"{\"key\": \"value\", "
		" \"one_more_key\": {\"key2\": \"value2\"}}";
	struct aajson a;

	aajson_init(&a, json);

	aajson_parse(&a, &callback, NULL);

	if (a.error) {
		fprintf(stderr, "%lu:%lu: error: %s\n",
			a.line, a.col, a.errmsg);
	}

	return 0;
}

```
