#define _POSIX_C_SOURCE 200809L

#include "sfconfig.h"

#include "sfcolor.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHITFETCH_MAX_CONFIG_SIZE (1024 * 1024)

enum json_type {
	JSON_NULL,
	JSON_BOOL,
	JSON_NUMBER,
	JSON_STRING,
	JSON_ARRAY,
	JSON_OBJECT,
};

struct json_value;

struct json_member {
	char *key;
	struct json_value *value;
	struct json_member *next;
};

struct json_item {
	struct json_value *value;
	struct json_item *next;
};

struct json_value {
	enum json_type type;
	union {
		bool boolean;
		char *string;
		char *number;
		struct {
			struct json_item *head;
			size_t count;
		} array;
		struct {
			struct json_member *head;
			size_t count;
		} object;
	} as;
};

struct json_parser {
	const char *path;
	const char *src;
	size_t len;
	size_t pos;
	size_t line;
	size_t col;
	char *err;
	size_t err_cap;
};

struct strbuf {
	char *buf;
	size_t len;
	size_t cap;
};

static void json_free(struct json_value *value);

static int
config_error(char *err, size_t err_cap, const char *path, const char *fmt, ...)
{
	char msg[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	if (err != NULL && err_cap > 0) {
		if (path != NULL && path[0] != '\0')
			snprintf(err, err_cap, "%s: %s", path, msg);
		else
			snprintf(err, err_cap, "%s", msg);
	}
	return -1;
}

static char *
dup_range(const char *src, size_t len)
{
	char *out;

	out = malloc(len + 1);
	if (out == NULL)
		return NULL;
	memcpy(out, src, len);
	out[len] = '\0';
	return out;
}

static bool
strbuf_init(struct strbuf *buf)
{
	buf->cap = 32;
	buf->len = 0;
	buf->buf = malloc(buf->cap);
	if (buf->buf == NULL)
		return false;
	buf->buf[0] = '\0';
	return true;
}

static bool
strbuf_reserve(struct strbuf *buf, size_t extra)
{
	char *next;
	size_t need;
	size_t cap;

	need = buf->len + extra + 1;
	if (need <= buf->cap)
		return true;
	cap = buf->cap;
	while (cap < need)
		cap *= 2;
	next = realloc(buf->buf, cap);
	if (next == NULL)
		return false;
	buf->buf = next;
	buf->cap = cap;
	return true;
}

static bool
strbuf_append_char(struct strbuf *buf, char c)
{
	if (!strbuf_reserve(buf, 1))
		return false;
	buf->buf[buf->len++] = c;
	buf->buf[buf->len] = '\0';
	return true;
}

static bool
strbuf_append_bytes(struct strbuf *buf, const char *src, size_t len)
{
	if (!strbuf_reserve(buf, len))
		return false;
	memcpy(buf->buf + buf->len, src, len);
	buf->len += len;
	buf->buf[buf->len] = '\0';
	return true;
}

static void
parser_error(struct json_parser *parser, const char *fmt, ...)
{
	char msg[256];
	va_list ap;

	if (parser->err != NULL && parser->err[0] != '\0')
		return;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	if (parser->err != NULL && parser->err_cap > 0) {
		snprintf(parser->err, parser->err_cap, "%s:%zu:%zu: %s",
			parser->path, parser->line, parser->col, msg);
	}
}

static char
parser_peek(const struct json_parser *parser)
{
	if (parser->pos >= parser->len)
		return '\0';
	return parser->src[parser->pos];
}

static char
parser_next(struct json_parser *parser)
{
	char c;

	if (parser->pos >= parser->len)
		return '\0';
	c = parser->src[parser->pos++];
	if (c == '\n') {
		parser->line++;
		parser->col = 1;
	} else {
		parser->col++;
	}
	return c;
}

static bool
skip_ws_comments(struct json_parser *parser)
{
	for (;;) {
		char c;

		while (isspace((unsigned char)parser_peek(parser)))
			parser_next(parser);

		c = parser_peek(parser);
		if (c != '/' || parser->pos + 1 >= parser->len)
			return true;

		if (parser->src[parser->pos + 1] == '/') {
			while (parser_peek(parser) != '\0' && parser_peek(parser) != '\n')
				parser_next(parser);
			continue;
		}
		if (parser->src[parser->pos + 1] == '*') {
			bool closed = false;

			parser_next(parser);
			parser_next(parser);
			while (parser->pos < parser->len) {
				if (parser_peek(parser) == '*' && parser->pos + 1 < parser->len &&
					parser->src[parser->pos + 1] == '/') {
					parser_next(parser);
					parser_next(parser);
					closed = true;
					break;
				}
				parser_next(parser);
			}
			if (!closed) {
				parser_error(parser, "unterminated block comment");
				return false;
			}
			continue;
		}

		return true;
	}
}

static int
hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static bool
parse_hex4(struct json_parser *parser, unsigned int *out)
{
	unsigned int value = 0;
	int i;

	for (i = 0; i < 4; i++) {
		int v;
		char c;

		c = parser_peek(parser);
		v = hex_value(c);
		if (v < 0) {
			parser_error(parser, "invalid unicode escape");
			return false;
		}
		value = value * 16U + (unsigned int)v;
		parser_next(parser);
	}

	*out = value;
	return true;
}

static bool
append_utf8(struct strbuf *buf, unsigned int cp)
{
	char bytes[4];

	if (cp <= 0x7FU)
		return strbuf_append_char(buf, (char)cp);
	if (cp <= 0x7FFU) {
		bytes[0] = (char)(0xC0U | (cp >> 6));
		bytes[1] = (char)(0x80U | (cp & 0x3FU));
		return strbuf_append_bytes(buf, bytes, 2);
	}
	if (cp <= 0xFFFFU) {
		bytes[0] = (char)(0xE0U | (cp >> 12));
		bytes[1] = (char)(0x80U | ((cp >> 6) & 0x3FU));
		bytes[2] = (char)(0x80U | (cp & 0x3FU));
		return strbuf_append_bytes(buf, bytes, 3);
	}
	if (cp <= 0x10FFFFU) {
		bytes[0] = (char)(0xF0U | (cp >> 18));
		bytes[1] = (char)(0x80U | ((cp >> 12) & 0x3FU));
		bytes[2] = (char)(0x80U | ((cp >> 6) & 0x3FU));
		bytes[3] = (char)(0x80U | (cp & 0x3FU));
		return strbuf_append_bytes(buf, bytes, 4);
	}
	return false;
}

static bool
parse_unicode_escape(struct json_parser *parser, struct strbuf *buf)
{
	unsigned int cp;

	if (!parse_hex4(parser, &cp))
		return false;

	if (cp >= 0xD800U && cp <= 0xDBFFU) {
		unsigned int low;

		if (parser_peek(parser) != '\\' || parser->pos + 1 >= parser->len ||
			parser->src[parser->pos + 1] != 'u') {
			parser_error(parser, "high surrogate must be followed by another unicode escape");
			return false;
		}
		parser_next(parser);
		parser_next(parser);
		if (!parse_hex4(parser, &low))
			return false;
		if (low < 0xDC00U || low > 0xDFFFU) {
			parser_error(parser, "invalid low surrogate");
			return false;
		}
		cp = 0x10000U + ((cp - 0xD800U) << 10) + (low - 0xDC00U);
	} else if (cp >= 0xDC00U && cp <= 0xDFFFU) {
		parser_error(parser, "low surrogate without high surrogate");
		return false;
	}

	if (!append_utf8(buf, cp)) {
		parser_error(parser, "out of memory");
		return false;
	}
	return true;
}

static char *
parse_string_raw(struct json_parser *parser)
{
	struct strbuf buf;

	if (parser_next(parser) != '"') {
		parser_error(parser, "expected string");
		return NULL;
	}
	if (!strbuf_init(&buf)) {
		parser_error(parser, "out of memory");
		return NULL;
	}

	for (;;) {
		unsigned char c;

		if (parser->pos >= parser->len) {
			parser_error(parser, "unterminated string");
			free(buf.buf);
			return NULL;
		}

		c = (unsigned char)parser_next(parser);
		if (c == '"')
			return buf.buf;
		if (c < 0x20U) {
			parser_error(parser, "control character in string");
			free(buf.buf);
			return NULL;
		}
		if (c == '\\') {
			char esc;

			if (parser->pos >= parser->len) {
				parser_error(parser, "unterminated escape");
				free(buf.buf);
				return NULL;
			}
			esc = parser_next(parser);
			switch (esc) {
			case '"':
			case '\\':
			case '/':
				if (!strbuf_append_char(&buf, esc))
					goto oom;
				break;
			case 'b':
				if (!strbuf_append_char(&buf, '\b'))
					goto oom;
				break;
			case 'f':
				if (!strbuf_append_char(&buf, '\f'))
					goto oom;
				break;
			case 'n':
				if (!strbuf_append_char(&buf, '\n'))
					goto oom;
				break;
			case 'r':
				if (!strbuf_append_char(&buf, '\r'))
					goto oom;
				break;
			case 't':
				if (!strbuf_append_char(&buf, '\t'))
					goto oom;
				break;
			case 'u':
				if (!parse_unicode_escape(parser, &buf)) {
					free(buf.buf);
					return NULL;
				}
				break;
			default:
				parser_error(parser, "invalid escape sequence");
				free(buf.buf);
				return NULL;
			}
			continue;
		}
		if (!strbuf_append_char(&buf, (char)c))
			goto oom;
	}

oom:
	parser_error(parser, "out of memory");
	free(buf.buf);
	return NULL;
}

static struct json_value *parse_value(struct json_parser *parser);

static struct json_value *
json_alloc(enum json_type type)
{
	struct json_value *value;

	value = calloc(1, sizeof(*value));
	if (value == NULL)
		return NULL;
	value->type = type;
	return value;
}

static bool
parse_literal(struct json_parser *parser, const char *literal)
{
	size_t i;

	for (i = 0; literal[i] != '\0'; i++) {
		if (parser_peek(parser) != literal[i]) {
			parser_error(parser, "expected %s", literal);
			return false;
		}
		parser_next(parser);
	}
	return true;
}

static struct json_value *
parse_number(struct json_parser *parser)
{
	struct json_value *value;
	size_t start;

	start = parser->pos;
	if (parser_peek(parser) == '-')
		parser_next(parser);
	if (parser_peek(parser) == '0') {
		parser_next(parser);
	} else if (parser_peek(parser) >= '1' && parser_peek(parser) <= '9') {
		while (isdigit((unsigned char)parser_peek(parser)))
			parser_next(parser);
	} else {
		parser_error(parser, "invalid number");
		return NULL;
	}

	if (parser_peek(parser) == '.') {
		parser_next(parser);
		if (!isdigit((unsigned char)parser_peek(parser))) {
			parser_error(parser, "invalid number");
			return NULL;
		}
		while (isdigit((unsigned char)parser_peek(parser)))
			parser_next(parser);
	}

	if (parser_peek(parser) == 'e' || parser_peek(parser) == 'E') {
		parser_next(parser);
		if (parser_peek(parser) == '+' || parser_peek(parser) == '-')
			parser_next(parser);
		if (!isdigit((unsigned char)parser_peek(parser))) {
			parser_error(parser, "invalid number");
			return NULL;
		}
		while (isdigit((unsigned char)parser_peek(parser)))
			parser_next(parser);
	}

	value = json_alloc(JSON_NUMBER);
	if (value == NULL) {
		parser_error(parser, "out of memory");
		return NULL;
	}
	value->as.number = dup_range(parser->src + start, parser->pos - start);
	if (value->as.number == NULL) {
		json_free(value);
		parser_error(parser, "out of memory");
		return NULL;
	}
	return value;
}

static bool
object_has_key(const struct json_value *object, const char *key)
{
	const struct json_member *member;

	for (member = object->as.object.head; member != NULL; member = member->next) {
		if (strcmp(member->key, key) == 0)
			return true;
	}
	return false;
}

static struct json_value *
parse_object(struct json_parser *parser)
{
	struct json_value *object;
	struct json_member **tail;

	if (parser_next(parser) != '{') {
		parser_error(parser, "expected object");
		return NULL;
	}
	object = json_alloc(JSON_OBJECT);
	if (object == NULL) {
		parser_error(parser, "out of memory");
		return NULL;
	}
	tail = &object->as.object.head;

	if (!skip_ws_comments(parser))
		goto fail;
	if (parser_peek(parser) == '}') {
		parser_next(parser);
		return object;
	}

	for (;;) {
		struct json_member *member;

		if (parser_peek(parser) != '"') {
			parser_error(parser, "expected object key");
			goto fail;
		}
		member = calloc(1, sizeof(*member));
		if (member == NULL) {
			parser_error(parser, "out of memory");
			goto fail;
		}
		member->key = parse_string_raw(parser);
		if (member->key == NULL) {
			free(member);
			goto fail;
		}
		if (object_has_key(object, member->key)) {
			parser_error(parser, "duplicate key '%s'", member->key);
			free(member->key);
			free(member);
			goto fail;
		}
		if (!skip_ws_comments(parser))
			goto member_fail;
		if (parser_next(parser) != ':') {
			parser_error(parser, "expected ':' after object key");
			goto member_fail;
		}
		if (!skip_ws_comments(parser))
			goto member_fail;
		member->value = parse_value(parser);
		if (member->value == NULL)
			goto member_fail;

		*tail = member;
		tail = &member->next;
		object->as.object.count++;

		if (!skip_ws_comments(parser))
			goto fail;
		if (parser_peek(parser) == ',') {
			parser_next(parser);
			if (!skip_ws_comments(parser))
				goto fail;
			if (parser_peek(parser) == '}') {
				parser_next(parser);
				return object;
			}
			continue;
		}
		if (parser_peek(parser) == '}') {
			parser_next(parser);
			return object;
		}
		parser_error(parser, "expected ',' or '}'");
		goto fail;

member_fail:
		free(member->key);
		json_free(member->value);
		free(member);
		goto fail;
	}

fail:
	json_free(object);
	return NULL;
}

static struct json_value *
parse_array(struct json_parser *parser)
{
	struct json_value *array;
	struct json_item **tail;

	if (parser_next(parser) != '[') {
		parser_error(parser, "expected array");
		return NULL;
	}
	array = json_alloc(JSON_ARRAY);
	if (array == NULL) {
		parser_error(parser, "out of memory");
		return NULL;
	}
	tail = &array->as.array.head;

	if (!skip_ws_comments(parser))
		goto fail;
	if (parser_peek(parser) == ']') {
		parser_next(parser);
		return array;
	}

	for (;;) {
		struct json_item *item;

		item = calloc(1, sizeof(*item));
		if (item == NULL) {
			parser_error(parser, "out of memory");
			goto fail;
		}
		item->value = parse_value(parser);
		if (item->value == NULL) {
			free(item);
			goto fail;
		}

		*tail = item;
		tail = &item->next;
		array->as.array.count++;

		if (!skip_ws_comments(parser))
			goto fail;
		if (parser_peek(parser) == ',') {
			parser_next(parser);
			if (!skip_ws_comments(parser))
				goto fail;
			if (parser_peek(parser) == ']') {
				parser_next(parser);
				return array;
			}
			continue;
		}
		if (parser_peek(parser) == ']') {
			parser_next(parser);
			return array;
		}
		parser_error(parser, "expected ',' or ']'");
		goto fail;
	}

fail:
	json_free(array);
	return NULL;
}

static struct json_value *
parse_value(struct json_parser *parser)
{
	struct json_value *value;
	char c;

	if (!skip_ws_comments(parser))
		return NULL;
	c = parser_peek(parser);
	if (c == '"') {
		value = json_alloc(JSON_STRING);
		if (value == NULL) {
			parser_error(parser, "out of memory");
			return NULL;
		}
		value->as.string = parse_string_raw(parser);
		if (value->as.string == NULL) {
			json_free(value);
			return NULL;
		}
		return value;
	}
	if (c == '{')
		return parse_object(parser);
	if (c == '[')
		return parse_array(parser);
	if (c == 't') {
		value = json_alloc(JSON_BOOL);
		if (value == NULL) {
			parser_error(parser, "out of memory");
			return NULL;
		}
		if (!parse_literal(parser, "true")) {
			json_free(value);
			return NULL;
		}
		value->as.boolean = true;
		return value;
	}
	if (c == 'f') {
		value = json_alloc(JSON_BOOL);
		if (value == NULL) {
			parser_error(parser, "out of memory");
			return NULL;
		}
		if (!parse_literal(parser, "false")) {
			json_free(value);
			return NULL;
		}
		value->as.boolean = false;
		return value;
	}
	if (c == 'n') {
		value = json_alloc(JSON_NULL);
		if (value == NULL) {
			parser_error(parser, "out of memory");
			return NULL;
		}
		if (!parse_literal(parser, "null")) {
			json_free(value);
			return NULL;
		}
		return value;
	}
	if (c == '-' || (c >= '0' && c <= '9'))
		return parse_number(parser);

	parser_error(parser, "expected value");
	return NULL;
}

static struct json_value *
json_parse(const char *path, const char *src, size_t len, char *err, size_t err_cap)
{
	struct json_parser parser;
	struct json_value *root;

	memset(&parser, 0, sizeof(parser));
	parser.path = path;
	parser.src = src;
	parser.len = len;
	parser.line = 1;
	parser.col = 1;
	parser.err = err;
	parser.err_cap = err_cap;

	root = parse_value(&parser);
	if (root == NULL)
		return NULL;
	if (!skip_ws_comments(&parser)) {
		json_free(root);
		return NULL;
	}
	if (parser.pos < parser.len) {
		parser_error(&parser, "unexpected trailing content");
		json_free(root);
		return NULL;
	}
	return root;
}

static void
json_free(struct json_value *value)
{
	if (value == NULL)
		return;
	switch (value->type) {
	case JSON_STRING:
		free(value->as.string);
		break;
	case JSON_NUMBER:
		free(value->as.number);
		break;
	case JSON_ARRAY: {
		struct json_item *item = value->as.array.head;

		while (item != NULL) {
			struct json_item *next = item->next;

			json_free(item->value);
			free(item);
			item = next;
		}
		break;
	}
	case JSON_OBJECT: {
		struct json_member *member = value->as.object.head;

		while (member != NULL) {
			struct json_member *next = member->next;

			free(member->key);
			json_free(member->value);
			free(member);
			member = next;
		}
		break;
	}
	case JSON_NULL:
	case JSON_BOOL:
		break;
	}
	free(value);
}

static const struct json_value *
object_get(const struct json_value *object, const char *key)
{
	const struct json_member *member;

	if (object == NULL || object->type != JSON_OBJECT)
		return NULL;
	for (member = object->as.object.head; member != NULL; member = member->next) {
		if (strcmp(member->key, key) == 0)
			return member->value;
	}
	return NULL;
}

static bool
key_allowed(const char *key, const char *const *allowed)
{
	size_t i;

	for (i = 0; allowed[i] != NULL; i++) {
		if (strcmp(key, allowed[i]) == 0)
			return true;
	}
	return false;
}

static int
validate_keys(const struct json_value *object, const char *const *allowed,
	const char *ctx, const char *path, char *err, size_t err_cap)
{
	const struct json_member *member;

	for (member = object->as.object.head; member != NULL; member = member->next) {
		if (!key_allowed(member->key, allowed))
			return config_error(err, err_cap, path, "unknown %s key '%s'", ctx, member->key);
	}
	return 0;
}

static int
copy_string_field(char *dst, size_t dst_cap, const char *src, const char *field,
	const char *path, char *err, size_t err_cap)
{
	if (strlen(src) >= dst_cap)
		return config_error(err, err_cap, path, "'%s' is too long", field);
	snprintf(dst, dst_cap, "%s", src);
	return 0;
}

static int
expect_string(const struct json_value *value, const char *field, const char *path,
	char *err, size_t err_cap, const char **out)
{
	if (value == NULL || value->type != JSON_STRING)
		return config_error(err, err_cap, path, "'%s' must be a string", field);
	*out = value->as.string;
	return 0;
}

static int
expect_bool(const struct json_value *value, const char *field, const char *path,
	char *err, size_t err_cap, bool *out)
{
	if (value == NULL || value->type != JSON_BOOL)
		return config_error(err, err_cap, path, "'%s' must be a boolean", field);
	*out = value->as.boolean;
	return 0;
}

static bool
color_valid(const char *spec, bool allow_empty)
{
	char out[32];

	if (spec[0] == '\0')
		return allow_empty;
	return sfcolor_resolve(spec, "36", out, sizeof(out));
}

static int
copy_color_field(char *dst, size_t dst_cap, const char *src, const char *field,
	bool allow_empty, const char *path, char *err, size_t err_cap)
{
	if (!color_valid(src, allow_empty))
		return config_error(err, err_cap, path, "'%s' has invalid color '%s'", field, src);
	return copy_string_field(dst, dst_cap, src, field, path, err, err_cap);
}

static int
template_from_value(const struct json_value *value, const char *path, char *err,
	size_t err_cap, enum shitfetch_template *out)
{
	const char *name;

	if (expect_string(value, "template", path, err, err_cap, &name) < 0)
		return -1;
	if (strcmp(name, "default") == 0) {
		*out = SHITFETCH_TEMPLATE_DEFAULT;
		return 0;
	}
	if (strcmp(name, "mini") == 0) {
		*out = SHITFETCH_TEMPLATE_MINI;
		return 0;
	}
	return config_error(err, err_cap, path, "unknown template '%s'", name);
}

static int
module_from_name(const char *name, enum shitfetch_module *out)
{
	if (strcmp(name, "os") == 0) *out = SHITFETCH_MODULE_OS;
	else if (strcmp(name, "kernel") == 0) *out = SHITFETCH_MODULE_KERNEL;
	else if (strcmp(name, "init") == 0) *out = SHITFETCH_MODULE_INIT;
	else if (strcmp(name, "uptime") == 0) *out = SHITFETCH_MODULE_UPTIME;
	else if (strcmp(name, "host") == 0) *out = SHITFETCH_MODULE_HOST;
	else if (strcmp(name, "shell") == 0) *out = SHITFETCH_MODULE_SHELL;
	else if (strcmp(name, "wm") == 0 || strcmp(name, "dewm") == 0 ||
		strcmp(name, "wm/de") == 0) *out = SHITFETCH_MODULE_DEWM;
	else if (strcmp(name, "term") == 0 || strcmp(name, "terminal") == 0) *out = SHITFETCH_MODULE_TERM;
	else if (strcmp(name, "cpu") == 0) *out = SHITFETCH_MODULE_CPU;
	else if (strcmp(name, "gpu") == 0) *out = SHITFETCH_MODULE_GPU;
	else if (strcmp(name, "memory") == 0) *out = SHITFETCH_MODULE_MEMORY;
	else if (strcmp(name, "swap") == 0) *out = SHITFETCH_MODULE_SWAP;
	else if (strcmp(name, "disk") == 0) *out = SHITFETCH_MODULE_DISK;
	else if (strcmp(name, "packages") == 0 || strcmp(name, "pkgs") == 0) *out = SHITFETCH_MODULE_PACKAGES;
	else if (strcmp(name, "display") == 0) *out = SHITFETCH_MODULE_DISPLAY;
	else return -1;
	return 0;
}

static bool
module_order_contains(const struct shitfetch_settings *settings, enum shitfetch_module module)
{
	size_t i;

	for (i = 0; i < settings->module_count; i++) {
		if (settings->module_order[i] == module)
			return true;
	}
	return false;
}

static int
enable_module(struct shitfetch_settings *settings, enum shitfetch_module module,
	const char *path, char *err, size_t err_cap)
{
	settings->module_enabled[module] = true;
	if (module_order_contains(settings, module))
		return 0;
	if (settings->module_count >= SHITFETCH_MAX_MODULES)
		return config_error(err, err_cap, path, "too many modules");
	settings->module_order[settings->module_count++] = module;
	return 0;
}

static void
clear_modules(struct shitfetch_settings *settings)
{
	size_t i;

	for (i = 0; i < SHITFETCH_MODULE_COUNT; i++)
		settings->module_enabled[i] = false;
	settings->module_count = 0;
}

static int
append_config_module_entry(struct shitfetch_settings *settings, enum shitfetch_module module,
	const char *path, char *err, size_t err_cap)
{
	size_t idx;

	if (settings->entry_count >= SHITFETCH_MAX_MODULE_ENTRIES)
		return config_error(err, err_cap, path, "too many entries");
	idx = settings->entry_count++;
	memset(&settings->entries[idx], 0, sizeof(settings->entries[idx]));
	settings->entries[idx].kind = SHITFETCH_ENTRY_MODULE;
	settings->entries[idx].module = module;
	settings->entries[idx].enabled = true;
	return 0;
}

static int
append_config_colors_entry(struct shitfetch_settings *settings, bool enabled,
	const char *path, char *err, size_t err_cap)
{
	size_t idx;

	if (settings->entry_count >= SHITFETCH_MAX_MODULE_ENTRIES)
		return config_error(err, err_cap, path, "too many entries");
	idx = settings->entry_count++;
	memset(&settings->entries[idx], 0, sizeof(settings->entries[idx]));
	settings->entries[idx].kind = SHITFETCH_ENTRY_COLORS;
	settings->entries[idx].enabled = enabled;
	return 0;
}

static int
apply_modules(struct shitfetch_settings *settings, const struct json_value *value,
	bool emit_entries, const char *path, char *err, size_t err_cap)
{
	const struct json_item *item;
	bool seen[SHITFETCH_MODULE_COUNT];
	bool seen_colors = false;

	if (value->type != JSON_ARRAY)
		return config_error(err, err_cap, path, "'modules' must be an array");

	memset(seen, 0, sizeof(seen));
	clear_modules(settings);
	if (emit_entries)
		settings->entry_count = 0;

	for (item = value->as.array.head; item != NULL; item = item->next) {
		enum shitfetch_module module;
		const char *name;

		if (item->value->type != JSON_STRING)
			return config_error(err, err_cap, path, "'modules' entries must be strings");
		name = item->value->as.string;
		if (strcmp(name, "colors") == 0) {
			if (seen_colors)
				return config_error(err, err_cap, path, "duplicate modules entry 'colors'");
			seen_colors = true;
			if (emit_entries &&
				append_config_colors_entry(settings, true, path, err, err_cap) < 0)
				return -1;
			continue;
		}
		if (module_from_name(name, &module) < 0)
			return config_error(err, err_cap, path, "unknown module '%s'", name);
		if (seen[module])
			return config_error(err, err_cap, path, "duplicate module '%s'", name);
		seen[module] = true;
		if (enable_module(settings, module, path, err, err_cap) < 0)
			return -1;
		if (emit_entries && append_config_module_entry(settings, module, path, err, err_cap) < 0)
			return -1;
	}
	return 0;
}

static int
apply_colors(struct shitfetch_settings *settings, const struct json_value *value,
	const char *path, char *err, size_t err_cap)
{
	const char *const allowed[] = {"key", "value", "header", "border", "custom", "logo", NULL};
	const struct json_value *field;
	const char *s;

	if (value->type != JSON_OBJECT)
		return config_error(err, err_cap, path, "'colors' must be an object");
	if (validate_keys(value, allowed, "colors", path, err, err_cap) < 0)
		return -1;

	field = object_get(value, "key");
	if (field != NULL) {
		if (expect_string(field, "colors.key", path, err, err_cap, &s) < 0 ||
			copy_color_field(settings->key_color_spec, sizeof(settings->key_color_spec),
				s, "colors.key", false, path, err, err_cap) < 0)
			return -1;
	}
	field = object_get(value, "value");
	if (field != NULL) {
		if (expect_string(field, "colors.value", path, err, err_cap, &s) < 0 ||
			copy_color_field(settings->value_color_spec, sizeof(settings->value_color_spec),
				s, "colors.value", true, path, err, err_cap) < 0)
			return -1;
	}
	field = object_get(value, "header");
	if (field != NULL) {
		if (expect_string(field, "colors.header", path, err, err_cap, &s) < 0 ||
			copy_color_field(settings->header_color_spec, sizeof(settings->header_color_spec),
				s, "colors.header", true, path, err, err_cap) < 0)
			return -1;
	}
	field = object_get(value, "border");
	if (field != NULL) {
		if (expect_string(field, "colors.border", path, err, err_cap, &s) < 0 ||
			copy_color_field(settings->border_color_spec, sizeof(settings->border_color_spec),
				s, "colors.border", true, path, err, err_cap) < 0)
			return -1;
	}
	field = object_get(value, "custom");
	if (field != NULL) {
		if (expect_string(field, "colors.custom", path, err, err_cap, &s) < 0 ||
			copy_color_field(settings->custom_color_spec, sizeof(settings->custom_color_spec),
				s, "colors.custom", true, path, err, err_cap) < 0)
			return -1;
	}
	field = object_get(value, "logo");
	if (field != NULL) {
		if (expect_string(field, "colors.logo", path, err, err_cap, &s) < 0 ||
			copy_color_field(settings->logo_color_spec, sizeof(settings->logo_color_spec),
				s, "colors.logo", true, path, err, err_cap) < 0)
			return -1;
	}
	return 0;
}

static int
apply_disk(struct shitfetch_settings *settings, const struct json_value *value,
	const char *path, char *err, size_t err_cap)
{
	const char *const allowed[] = {"all", "showFs", "mounts", NULL};
	const struct json_value *field;
	bool b;

	if (value->type != JSON_OBJECT)
		return config_error(err, err_cap, path, "'disk' must be an object");
	if (validate_keys(value, allowed, "disk", path, err, err_cap) < 0)
		return -1;

	field = object_get(value, "all");
	if (field != NULL) {
		if (expect_bool(field, "disk.all", path, err, err_cap, &b) < 0)
			return -1;
		settings->disk_all = b;
	}
	field = object_get(value, "showFs");
	if (field != NULL) {
		if (expect_bool(field, "disk.showFs", path, err, err_cap, &b) < 0)
			return -1;
		settings->disk_show_fs = b;
	}
	field = object_get(value, "mounts");
	if (field != NULL) {
		const struct json_item *item;

		if (field->type != JSON_ARRAY)
			return config_error(err, err_cap, path, "'disk.mounts' must be an array");
		if (field->as.array.count > SHITFETCH_MAX_DISK_FILTERS)
			return config_error(err, err_cap, path, "too many disk mounts");
		settings->disk_mount_filter_count = 0;
		for (item = field->as.array.head; item != NULL; item = item->next) {
			const char *mount;

			if (expect_string(item->value, "disk.mounts[]", path, err, err_cap, &mount) < 0)
				return -1;
			if (copy_string_field(settings->disk_mount_filter[settings->disk_mount_filter_count],
				sizeof(settings->disk_mount_filter[settings->disk_mount_filter_count]),
				mount, "disk.mounts[]", path, err, err_cap) < 0)
				return -1;
			settings->disk_mount_filter_count++;
		}
	}
	return 0;
}

static int
entry_selector_count(const struct json_value *object)
{
	int count = 0;

	if (object_get(object, "module") != NULL) count++;
	if (object_get(object, "break") != NULL) count++;
	if (object_get(object, "separator") != NULL) count++;
	if (object_get(object, "custom") != NULL) count++;
	if (object_get(object, "colors") != NULL) count++;
	return count;
}

static int
append_entry_from_object(struct shitfetch_settings *settings, const struct json_value *entry,
	const char *path, char *err, size_t err_cap)
{
	const char *const allowed[] = {
		"module", "break", "separator", "custom", "colors",
		"enabled", "key", "keyColor", "format", NULL
	};
	const struct json_value *field;
	const char *s;
	bool enabled = true;
	size_t idx;

	if (entry->type != JSON_OBJECT)
		return config_error(err, err_cap, path, "'entries' items must be objects");
	if (validate_keys(entry, allowed, "entry", path, err, err_cap) < 0)
		return -1;
	if (entry_selector_count(entry) != 1)
		return config_error(err, err_cap, path,
			"each entry must set exactly one of module, break, separator, custom, or colors");
	field = object_get(entry, "enabled");
	if (field != NULL && expect_bool(field, "entry.enabled", path, err, err_cap, &enabled) < 0)
		return -1;
	if (settings->entry_count >= SHITFETCH_MAX_MODULE_ENTRIES)
		return config_error(err, err_cap, path, "too many entries");

	idx = settings->entry_count++;
	memset(&settings->entries[idx], 0, sizeof(settings->entries[idx]));
	settings->entries[idx].enabled = enabled;

	field = object_get(entry, "module");
	if (field != NULL) {
		enum shitfetch_module module;

		if (expect_string(field, "entry.module", path, err, err_cap, &s) < 0)
			return -1;
		if (module_from_name(s, &module) < 0)
			return config_error(err, err_cap, path, "unknown module '%s'", s);
		settings->entries[idx].kind = SHITFETCH_ENTRY_MODULE;
		settings->entries[idx].module = module;
		if (enabled && enable_module(settings, module, path, err, err_cap) < 0)
			return -1;

		field = object_get(entry, "key");
		if (field != NULL) {
			if (expect_string(field, "entry.key", path, err, err_cap, &s) < 0 ||
				copy_string_field(settings->entries[idx].key, sizeof(settings->entries[idx].key),
					s, "entry.key", path, err, err_cap) < 0)
				return -1;
			settings->entries[idx].key_set = true;
		}
		field = object_get(entry, "keyColor");
		if (field != NULL) {
			if (expect_string(field, "entry.keyColor", path, err, err_cap, &s) < 0 ||
				copy_color_field(settings->entries[idx].key_color,
					sizeof(settings->entries[idx].key_color), s, "entry.keyColor",
					false, path, err, err_cap) < 0)
				return -1;
			settings->entries[idx].key_color_set = true;
		}
		field = object_get(entry, "format");
		if (field != NULL) {
			if (expect_string(field, "entry.format", path, err, err_cap, &s) < 0 ||
				copy_string_field(settings->entries[idx].format,
					sizeof(settings->entries[idx].format), s, "entry.format",
					path, err, err_cap) < 0)
				return -1;
			settings->entries[idx].format_set = true;
		}
		return 0;
	}

	if (object_get(entry, "key") != NULL || object_get(entry, "keyColor") != NULL ||
		object_get(entry, "format") != NULL)
		return config_error(err, err_cap, path,
			"entry key, keyColor, and format are only valid for module entries");

	field = object_get(entry, "break");
	if (field != NULL) {
		bool selector;

		if (expect_bool(field, "entry.break", path, err, err_cap, &selector) < 0)
			return -1;
		if (!selector)
			return config_error(err, err_cap, path, "'entry.break' must be true");
		settings->entries[idx].kind = SHITFETCH_ENTRY_BREAK;
		return 0;
	}
	field = object_get(entry, "separator");
	if (field != NULL) {
		if (expect_string(field, "entry.separator", path, err, err_cap, &s) < 0 ||
			copy_string_field(settings->entries[idx].text, sizeof(settings->entries[idx].text),
				s, "entry.separator", path, err, err_cap) < 0)
			return -1;
		settings->entries[idx].kind = SHITFETCH_ENTRY_SEPARATOR;
		return 0;
	}
	field = object_get(entry, "custom");
	if (field != NULL) {
		if (expect_string(field, "entry.custom", path, err, err_cap, &s) < 0 ||
			copy_string_field(settings->entries[idx].text, sizeof(settings->entries[idx].text),
				s, "entry.custom", path, err, err_cap) < 0)
			return -1;
		settings->entries[idx].kind = SHITFETCH_ENTRY_CUSTOM;
		return 0;
	}
	field = object_get(entry, "colors");
	if (field != NULL) {
		bool selector;

		if (expect_bool(field, "entry.colors", path, err, err_cap, &selector) < 0)
			return -1;
		if (!selector)
			return config_error(err, err_cap, path, "'entry.colors' must be true");
		settings->entries[idx].kind = SHITFETCH_ENTRY_COLORS;
		return 0;
	}

	return config_error(err, err_cap, path, "invalid entry");
}

static int
apply_entries(struct shitfetch_settings *settings, const struct json_value *value,
	bool modules_present, const char *path, char *err, size_t err_cap)
{
	const struct json_item *item;

	if (value->type != JSON_ARRAY)
		return config_error(err, err_cap, path, "'entries' must be an array");
	if (!modules_present)
		clear_modules(settings);
	settings->entry_count = 0;
	for (item = value->as.array.head; item != NULL; item = item->next) {
		if (append_entry_from_object(settings, item->value, path, err, err_cap) < 0)
			return -1;
	}
	return 0;
}

static int
read_config_file(const char *path, char **out, size_t *out_len, char *err, size_t err_cap)
{
	FILE *fp;
	long len;
	char *buf;
	size_t got;

	fp = fopen(path, "rb");
	if (fp == NULL)
		return config_error(err, err_cap, path, "%s", strerror(errno));
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return config_error(err, err_cap, path, "could not seek file");
	}
	len = ftell(fp);
	if (len < 0) {
		fclose(fp);
		return config_error(err, err_cap, path, "could not determine file size");
	}
	if (len > SHITFETCH_MAX_CONFIG_SIZE) {
		fclose(fp);
		return config_error(err, err_cap, path, "file is too large");
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return config_error(err, err_cap, path, "could not rewind file");
	}
	buf = malloc((size_t)len + 1);
	if (buf == NULL) {
		fclose(fp);
		return config_error(err, err_cap, path, "out of memory");
	}
	got = fread(buf, 1, (size_t)len, fp);
	if (got != (size_t)len || ferror(fp)) {
		free(buf);
		fclose(fp);
		return config_error(err, err_cap, path, "could not read file");
	}
	fclose(fp);
	buf[got] = '\0';
	*out = buf;
	*out_len = got;
	return 0;
}

bool
shitfetch_default_config_path(char *buf, size_t cap)
{
	const char *xdg;
	const char *home;
	int n;

	if (buf == NULL || cap == 0)
		return false;
	buf[0] = '\0';
	xdg = getenv("XDG_CONFIG_HOME");
	if (xdg != NULL && xdg[0] != '\0') {
		n = snprintf(buf, cap, "%s/shitfetch/config.jsonc", xdg);
		return n > 0 && (size_t)n < cap;
	}
	home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
		return false;
	n = snprintf(buf, cap, "%s/.config/shitfetch/config.jsonc", home);
	return n > 0 && (size_t)n < cap;
}

int
shitfetch_load_config(struct shitfetch_settings *settings, const char *path,
	char *err, size_t err_cap)
{
	static const char *const top_allowed[] = {
		"template", "logo", "header", "ansi", "separator", "asciiDir",
		"colors", "modules", "entries", "disk", NULL
	};
	char *buf = NULL;
	size_t len = 0;
	struct json_value *root;
	const struct json_value *field;
	enum shitfetch_template template;
	bool b;
	const char *s;
	bool modules_present;
	bool entries_present;

	if (err != NULL && err_cap > 0)
		err[0] = '\0';
	if (read_config_file(path, &buf, &len, err, err_cap) < 0)
		return -1;
	root = json_parse(path, buf, len, err, err_cap);
	free(buf);
	if (root == NULL)
		return -1;
	if (root->type != JSON_OBJECT) {
		json_free(root);
		return config_error(err, err_cap, path, "root value must be an object");
	}
	if (validate_keys(root, top_allowed, "top-level", path, err, err_cap) < 0)
		goto fail;

	field = object_get(root, "template");
	if (field != NULL) {
		if (template_from_value(field, path, err, err_cap, &template) < 0)
			goto fail;
		shitfetch_settings_apply_template(settings, template);
	}

	field = object_get(root, "logo");
	if (field != NULL) {
		if (expect_string(field, "logo", path, err, err_cap, &s) < 0 ||
			copy_string_field(settings->logo, sizeof(settings->logo), s, "logo",
				path, err, err_cap) < 0)
			goto fail;
		settings->show_logo = strcmp(settings->logo, "none") != 0;
	}
	field = object_get(root, "header");
	if (field != NULL) {
		if (expect_bool(field, "header", path, err, err_cap, &b) < 0)
			goto fail;
		settings->show_header = b;
	}
	field = object_get(root, "ansi");
	if (field != NULL) {
		if (expect_bool(field, "ansi", path, err, err_cap, &b) < 0)
			goto fail;
		settings->show_ansi = b;
	}
	field = object_get(root, "separator");
	if (field != NULL) {
		if (expect_string(field, "separator", path, err, err_cap, &s) < 0 ||
			copy_string_field(settings->separator, sizeof(settings->separator), s,
				"separator", path, err, err_cap) < 0)
			goto fail;
	}
	field = object_get(root, "asciiDir");
	if (field != NULL) {
		if (expect_string(field, "asciiDir", path, err, err_cap, &s) < 0 ||
			copy_string_field(settings->ascii_dir, sizeof(settings->ascii_dir), s,
				"asciiDir", path, err, err_cap) < 0)
			goto fail;
	}
	field = object_get(root, "colors");
	if (field != NULL && apply_colors(settings, field, path, err, err_cap) < 0)
		goto fail;
	field = object_get(root, "disk");
	if (field != NULL && apply_disk(settings, field, path, err, err_cap) < 0)
		goto fail;

	modules_present = object_get(root, "modules") != NULL;
	entries_present = object_get(root, "entries") != NULL;
	field = object_get(root, "modules");
	if (field != NULL && apply_modules(settings, field, !entries_present, path, err, err_cap) < 0)
		goto fail;
	field = object_get(root, "entries");
	if (field != NULL && apply_entries(settings, field, modules_present, path, err, err_cap) < 0)
		goto fail;

	json_free(root);
	return 0;

fail:
	json_free(root);
	return -1;
}
