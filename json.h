/*
 * Copyright (c) 2018 Julius Barzdziukas <julius.barzdziukas@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef JSON_H
#define JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

struct json_object{
	char *name;
	struct json_value *value;
	struct json_object *next;
};

struct json_value{
	uint8_t type;  // 0 no data, 1 object, 2 array/value, 3 string, 4 int, 5 double, 6 boolean.
	union{
		char *value_string;
		int64_t value_int;
		double value_double;
		uint8_t value_boolean;
		struct json_object *object;
		struct json_value *value;
	};
	struct json_value *next;
};

struct json_start{
	uint8_t type; // 0 no data, 1 object, 2 value
	union{
		struct json_object *object;
		struct json_value *value;
	};
};

int read_json(FILE *,struct json_start **);
int print_json(FILE *,struct json_start *);
int free_json(struct json_start **);
struct json_value * get_value(char *,struct json_object *);
char * get_string_first(char *,struct json_object *);

#ifdef __cplusplus
}
#endif 

#endif
