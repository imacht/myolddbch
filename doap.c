#include <dbch.h>


struct doap_obj {
	struct doap_obj *next;
	const char *name;
	const struct doap_attr *attr;
	void *ctx;
};


static struct doap_obj *objs;


// helpers

static char* skip_tag(char *p)
{
	char *anchor = p;
	while (*p && *p != '{' && *p != '}' && *p!= ',') {
		if (*p++ == ':')
			return p;
	}
	return anchor; // no tag
}

static const struct doap_attr* find_attr(struct doap_obj *o, char *attr)
{
	const struct doap_attr *a = o->attr;
	while (a->name) {
		if (strcmp(a->name, attr) == 0)
			return a;
		a++;
	}
	return 0;
}

static int usage(struct doap_obj *o)
{
	const struct doap_attr *a = o->attr;
	while (a->name) {
		uartf("* %s.%s: %s\n", o->name, a->name, a->desc);
		a++;
	}
	return -1;
}

static int root(void)
{
	struct doap_obj *o = objs;
	while (o) {
		uartf("%s\n", o->name);
		o = o->next;
	}
	return -1;
}


// exports

void doap_obj_add(const char *name, const struct doap_attr *attr, void *ctx)
{
	struct doap_obj *o = objs;
	while (o && strcmp(o->name, name))
		o = o->next;
	if (o);
	else if (o = zalloc(sizeof(*o))) {
		o->name = name;
		o->next = objs, objs = o;
	} else
		return;
	o->attr = attr;
	o->ctx = ctx;
}

void doap_obj_del(const char *name)
{
	struct doap_obj **pp = &objs, *p;
	while (p = *pp) {
		if (strcmp(p->name, name) == 0) {
			*pp = p->next;
			free(p);
		} else
			pp = &p->next;
	}
}

int doap_obj_parse(char *text, objp_f *got, void *ctx)
{
	text = skip_tag(text);
	if (*text++ != '{')
		return 0; // not an object

	int index = 0, depth = 0;
	while (*text && *text != '}') {
// TODO handle strings better
		char *element = text = skip_tag(text), tmp;
		if (*text == '{') { // sub-object
			do {
				if (*text == '{')
					depth++;
				else if (*text == '}')
					depth--;
			} while (*text++ && depth);
		} else {
			while (*text && *text != ',' && *text != '}')
				text++;
		}

		tmp = *text , *text = 0;
		if (got)
			got(index, element, ctx);
		*text = tmp;
		if (tmp == ',')
			text++;
		index++;
	}
	return index;
}

int doap_run(char *obj)
{
	if (strcmp(obj, "doap") == 0)
		return root();

	char *val = strchr(obj, '=');
	if (val)
		*val++ = 0;

	char *dot = strchr(obj, '.');
	if (dot)
		*dot++ = 0;

	struct doap_obj *o = objs;
	while (o && strcmp(obj, o->name))
		o = o->next;
	if (dot)
		dot[-1] = '.';
	if (!o) { // not DOAP
		if (val)
			*--val = '=';
		return 0;
	}

	if (!dot)
		return usage(o);

	const struct doap_attr *a = find_attr(o, dot);
	if (!a) {
		uartf("no %s\n", obj);
		return usage(o);
	}

	if (val)
		a->set ? a->set(o->ctx, val) : uartf("no %s setter\n", obj);
	else
		a->get ? a->get(o->ctx) : uartf("no %s getter\n", obj);
	return -1;
}

int64_t hex2int(char *h)
{
	uint64_t r = 0;
	char c, neg = 0;
	if (*h == '-')
		neg = 1, h++;

	while (c = *h++) {
		if ((c -= '0') >= 10)
			c = c - 7 & ~32;
		r = r << 4 | c;
	}

	return neg ? -r : r;
}

int hex2bin(char *h, uint8_t *b)
{
	uint32_t got = 0, c;
	while (c = *h++) {
		if ((c -= '0') >= 10)
			c = c - 7 & ~32;
		*b = *b << 4 | c;
		if (++got % 2 == 0)
			b++;
	}
	return got / 2;
}

int hex2nib(char *h, uint8_t *b)
{
	int len = hex2bin(h, b);

	uint8_t *e = b + len, tmp;
	while (b < --e)
		tmp = *e, *e = *b, *b++ = tmp;
	return len;
}

uint32_t hex2time(char *hex)
{
	if (*hex == '+')
		return emberAfGetCurrentTimeCallback() + hex2int(hex + 1);
	if (*hex == '-')
		return emberAfGetCurrentTimeCallback() - hex2int(hex + 1);
	return hex2int(hex);
}
