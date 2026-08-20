#define _POSIX_C_SOURCE 200809L

#include "sf.h"
#include "sfspin.h"
#include "sfcolor.h"
#include "sfutil.h"

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#define SPIN_LAYERS 5
#define SPIN_CAM_MUL 7.0f
#define SPIN_CAM_PAD 5.0f
#define SPIN_SPEC_POWER 8.0f
#define SPIN_NORMAL_FLAT 1.5f
#define SPIN_DITHER_FLOOR 0.35f
#define SPIN_MAX_SPAN 6
#define SPIN_MIN_COLS 12
#define SPIN_MIN_ROWS 6
#define SPIN_EMPTY 0xFFu

struct spin_point {
	float x, y, z;
	float nx, ny, nz;
	unsigned char color_idx;
};

struct spin_model {
	struct spin_point *points;
	size_t count;
	float radius;
	float ext_x;
	float ext_y;
	float ext_z;
};

struct spin_frame {
	char *ch;
	unsigned char *color;
	float *zbuf;
	int cols;
	int rows;
	int sub_x;
	int sub_y;
};

/* Right-hand column: the user@host header followed by the info lines, rebuilt whenever
   the live values tick. */
struct spin_side {
	char text[SHITFETCH_MAX_LINE * 2];
};

static float
ascii_density(unsigned int cp)
{
	static const struct {
		const char *chars;
		float weight;
	} tiers[] = {
		{".'`,^\"", 0.12f},
		{":;_-~!iIl|/\\", 0.22f},
		{"()[]{}<>=+?*rcvxzsenaoutfjy", 0.38f},
		{"1723459JLCTYZFVP", 0.52f},
		{"680kbdpqhmwgHSUXAEGKNRDOQ&%", 0.70f},
		{"#$MW@B", 0.95f},
	};
	size_t i;

	for (i = 0; i < sizeof(tiers) / sizeof(tiers[0]); i++) {
		if (strchr(tiers[i].chars, (int)cp) != NULL)
			return tiers[i].weight;
	}
	return 0.55f;
}

/* Visual ink weight of a glyph, 0..1. Zero means the cell is empty and contributes no
   geometry at all; everything else becomes relief height. */
static float
glyph_density(unsigned int cp)
{
	static const float quadrants[10] = {
		0.25f, 0.25f, 0.25f, 0.75f, 0.5f, 0.75f, 0.75f, 0.25f, 0.5f, 0.75f
	};

	if (cp == 0 || cp == ' ' || cp == '\t' || cp == 0x00A0u)
		return 0.0f;
	if (cp > ' ' && cp < 0x7Fu)
		return ascii_density(cp);
	if (cp >= 0x2800u && cp <= 0x28FFu) {
		unsigned int bits = cp & 0xFFu;
		unsigned int n = 0;

		while (bits != 0) {
			n += bits & 1u;
			bits >>= 1;
		}
		return (float)n / 8.0f;
	}
	if (cp == 0x2580u || cp == 0x2590u)
		return 0.5f;
	if (cp >= 0x2581u && cp <= 0x2588u)
		return (float)(cp - 0x2580u) / 8.0f;
	if (cp >= 0x2589u && cp <= 0x258Fu)
		return (float)(0x2590u - cp) / 8.0f;
	if (cp == 0x2591u)
		return 0.25f;
	if (cp == 0x2592u)
		return 0.5f;
	if (cp == 0x2593u)
		return 0.75f;
	if (cp == 0x2594u || cp == 0x2595u)
		return 0.125f;
	if (cp >= 0x2596u && cp <= 0x259Fu)
		return quadrants[cp - 0x2596u];
	if (cp >= 0x2500u && cp <= 0x257Fu)
		return 0.35f;
	if (cp >= 0x25A0u && cp <= 0x25FFu)
		return 0.6f;
	return 0.55f;
}

/* Counts printable columns, skipping CSI sequences and UTF-8 continuation bytes. */
static size_t
visible_width(const char *s)
{
	size_t w = 0;

	while (*s != '\0') {
		if (*s == '\033') {
			s++;
			if (*s == '[') {
				s++;
				while (*s != '\0' && !((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')))
					s++;
			}
			if (*s != '\0')
				s++;
			continue;
		}
		if (((unsigned char)*s & 0xC0u) != 0x80u)
			w++;
		s++;
	}
	return w;
}

static float
clampf(float v, float lo, float hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static void
vec_norm(float *x, float *y, float *z)
{
	float len = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));

	if (len <= 1e-6f) {
		*x = 0.0f;
		*y = 0.0f;
		*z = -1.0f;
		return;
	}
	*x /= len;
	*y /= len;
	*z /= len;
}

static float
height_at(const float *h, size_t rows, size_t cols, long row, long col)
{
	if (row < 0 || col < 0 || (size_t)row >= rows || (size_t)col >= cols)
		return 0.0f;
	return h[(size_t)row * cols + (size_t)col];
}

/* Uniform art (a logo drawn with a single repeated glyph) has almost no height variance
   and would render as a sheet of paper, so the flatter the source the thicker the slab. */
static float
auto_depth(const float *h, size_t n)
{
	double sum = 0.0;
	double sumsq = 0.0;
	size_t used = 0;
	size_t i;
	double mean;
	double var;
	float stddev;

	for (i = 0; i < n; i++) {
		if (h[i] <= 0.0f)
			continue;
		sum += h[i];
		sumsq += (double)h[i] * h[i];
		used++;
	}
	if (used == 0)
		return 3.0f;
	mean = sum / (double)used;
	var = sumsq / (double)used - mean * mean;
	stddev = var > 0.0 ? (float)sqrt(var) : 0.0f;
	return 1.0f + 4.0f * (1.0f - fminf(1.0f, stddev / 0.25f));
}

static void
add_point(struct spin_model *model, float x, float y, float z,
	float nx, float ny, float nz, unsigned char color_idx)
{
	struct spin_point *p = &model->points[model->count++];
	float r;

	p->x = x;
	p->y = y;
	p->z = z;
	vec_norm(&nx, &ny, &nz);
	p->nx = nx;
	p->ny = ny;
	p->nz = nz;
	p->color_idx = color_idx;
	r = sqrtf(x * x + y * y + z * z);
	if (r > model->radius)
		model->radius = r;
	if (fabsf(x) > model->ext_x)
		model->ext_x = fabsf(x);
	if (fabsf(y) > model->ext_y)
		model->ext_y = fabsf(y);
	if (fabsf(z) > model->ext_z)
		model->ext_z = fabsf(z);
}

/* Turns the logo grid into a point cloud: interior cells get SPIN_LAYERS slices for a
   solid extrusion, edge cells only front and back plus one outward-facing point per
   open side so the silhouette survives a 90 degree turn. */
static int
build_model(const struct shitfetch_logo_grid *grid, const struct shitfetch_spin *spin,
	struct spin_model *out)
{
	float *h;
	size_t n = grid->rows * grid->cols;
	size_t nonempty = 0;
	size_t i;
	size_t row;
	size_t col;
	float depth;

	memset(out, 0, sizeof(*out));
	h = malloc(n * sizeof(*h));
	if (h == NULL)
		return -1;
	for (i = 0; i < n; i++) {
		h[i] = glyph_density(grid->cells[i].cp);
		if (h[i] > 0.0f)
			nonempty++;
	}
	if (nonempty == 0) {
		free(h);
		return -1;
	}

	depth = spin->depth > 0.0f ? spin->depth : auto_depth(h, n);
	depth = clampf(depth, 0.1f, 10.0f);

	out->points = malloc(nonempty * (SPIN_LAYERS + 4) * sizeof(*out->points));
	if (out->points == NULL) {
		free(h);
		return -1;
	}

	for (row = 0; row < grid->rows; row++) {
		for (col = 0; col < grid->cols; col++) {
			float d = h[row * grid->cols + col];
			float hz;
			float wx;
			float wy;
			float dhx;
			float dhy;
			bool open_l;
			bool open_r;
			bool open_u;
			bool open_d;
			unsigned char ci;

			if (d <= 0.0f)
				continue;
			hz = d * depth;
			wx = (float)col - (float)(grid->cols - 1) * 0.5f;
			wy = ((float)(grid->rows - 1) * 0.5f - (float)row) * 2.0f;
			ci = grid->cells[row * grid->cols + col].color_idx;

			dhx = height_at(h, grid->rows, grid->cols, (long)row, (long)col + 1) -
				height_at(h, grid->rows, grid->cols, (long)row, (long)col - 1);
			dhy = height_at(h, grid->rows, grid->cols, (long)row - 1, (long)col) -
				height_at(h, grid->rows, grid->cols, (long)row + 1, (long)col);
			open_l = height_at(h, grid->rows, grid->cols, (long)row, (long)col - 1) <= 0.0f;
			open_r = height_at(h, grid->rows, grid->cols, (long)row, (long)col + 1) <= 0.0f;
			open_u = height_at(h, grid->rows, grid->cols, (long)row - 1, (long)col) <= 0.0f;
			open_d = height_at(h, grid->rows, grid->cols, (long)row + 1, (long)col) <= 0.0f;

			if (!open_l && !open_r && !open_u && !open_d) {
				int k;

				for (k = 0; k < SPIN_LAYERS; k++) {
					float t = (float)k / (float)(SPIN_LAYERS - 1);
					float z = -hz * 0.5f + t * hz;

					if (z < 0.0f)
						add_point(out, wx, wy, z, -dhx, -dhy, -SPIN_NORMAL_FLAT, ci);
					else
						add_point(out, wx, wy, z, dhx, dhy, SPIN_NORMAL_FLAT, ci);
				}
				continue;
			}
			add_point(out, wx, wy, -hz * 0.5f, -dhx, -dhy, -SPIN_NORMAL_FLAT, ci);
			add_point(out, wx, wy, hz * 0.5f, dhx, dhy, SPIN_NORMAL_FLAT, ci);
			if (open_l)
				add_point(out, wx, wy, 0.0f, -1.0f, 0.0f, 0.0f, ci);
			if (open_r)
				add_point(out, wx, wy, 0.0f, 1.0f, 0.0f, 0.0f, ci);
			if (open_u)
				add_point(out, wx, wy, 0.0f, 0.0f, 1.0f, 0.0f, ci);
			if (open_d)
				add_point(out, wx, wy, 0.0f, 0.0f, -1.0f, 0.0f, ci);
		}
	}
	free(h);
	if (out->radius <= 1e-3f)
		out->radius = 1.0f;
	return 0;
}

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_resized;
static bool g_term_active;
static int g_block_rows;

#ifdef _WIN32
static DWORD g_saved_in_mode;
static DWORD g_saved_out_mode;
#else
static struct termios g_saved_term;
#endif

static void
emit(const char *buf, size_t len)
{
#ifdef _WIN32
	(void)_write(_fileno(stdout), buf, (unsigned int)len);
#else
	size_t off = 0;

	while (off < len) {
		ssize_t n = write(STDOUT_FILENO, buf + off, len - off);

		if (n <= 0)
			break;
		off += (size_t)n;
	}
#endif
}

/* Walks back to the first row of the drawn block and wipes it. The animation lives in the
   normal screen right where the prompt left the cursor, so only those rows are ever
   touched: scrollback above them stays readable and nothing needs a full clear. */
static void
block_erase(void)
{
	char seq[32];
	int n;

	if (g_block_rows <= 0)
		return;
	if (g_block_rows > 1)
		n = snprintf(seq, sizeof(seq), "\r\033[%dA\033[J", g_block_rows - 1);
	else
		n = snprintf(seq, sizeof(seq), "\r\033[J");
	g_block_rows = 0;
	if (n > 0)
		emit(seq, (size_t)n);
}

static void
term_leave(void)
{
	if (!g_term_active)
		return;
	g_term_active = false;
	block_erase();
	emit("\033[?25h", 6);
#ifdef _WIN32
	SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), g_saved_in_mode);
	SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), g_saved_out_mode);
#else
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_term);
#endif
}

static void
on_stop(int sig)
{
	(void)sig;
	g_stop = 1;
}

#ifndef _WIN32
static void
on_winch(int sig)
{
	(void)sig;
	g_resized = 1;
}
#endif

static void
install_signals(void)
{
#ifdef _WIN32
	signal(SIGINT, on_stop);
	signal(SIGTERM, on_stop);
#else
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_stop;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sa.sa_handler = on_winch;
	sigaction(SIGWINCH, &sa, NULL);
#endif
}

static bool
term_enter(void)
{
#ifdef _WIN32
	HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD in_mode;
	DWORD out_mode;

	if (!_isatty(_fileno(stdout)))
		return false;
	if (!GetConsoleMode(in, &in_mode) || !GetConsoleMode(out, &out_mode))
		return false;
	g_saved_in_mode = in_mode;
	g_saved_out_mode = out_mode;
	if (!SetConsoleMode(out, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
		return false;
	SetConsoleMode(in, in_mode & (DWORD)~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));
#else
	struct termios raw;

	if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO))
		return false;
	if (tcgetattr(STDIN_FILENO, &g_saved_term) != 0)
		return false;
	raw = g_saved_term;
	raw.c_lflag &= (tcflag_t)~(ECHO | ICANON);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0)
		return false;
#endif
	g_term_active = true;
	atexit(term_leave);
	install_signals();
	fflush(stdout);
	emit("\033[?25l", 6);
	return true;
}

static void
term_size(int *cols, int *rows)
{
#ifdef _WIN32
	CONSOLE_SCREEN_BUFFER_INFO csbi;

	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
		*cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
		*rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
		if (*cols > 0 && *rows > 0)
			return;
	}
#else
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return;
	}
#endif
	*cols = 80;
	*rows = 24;
}

static bool
key_pressed(void)
{
#ifdef _WIN32
	if (_kbhit()) {
		(void)_getch();
		return true;
	}
	return false;
#else
	char c;

	return read(STDIN_FILENO, &c, 1) > 0;
#endif
}

static double
now_seconds(void)
{
#ifdef _WIN32
	return (double)GetTickCount64() / 1000.0;
#else
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0.0;
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

static void
sleep_seconds(double s)
{
	if (s <= 0.0)
		return;
#ifdef _WIN32
	Sleep((DWORD)(s * 1000.0));
#else
	struct timespec ts;

	ts.tv_sec = (time_t)s;
	ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
	nanosleep(&ts, NULL);
#endif
}

static void
light_vector(enum shitfetch_spin_light light, float *lx, float *ly, float *lz)
{
	static const float dirs[9][3] = {
		{-0.6f, 0.6f, -0.5f},   /* top-left */
		{0.0f, 0.7f, -0.7f},    /* top */
		{0.6f, 0.6f, -0.5f},    /* top-right */
		{-0.7f, 0.0f, -0.7f},   /* left */
		{0.0f, 0.0f, -1.0f},    /* front */
		{0.7f, 0.0f, -0.7f},    /* right */
		{-0.6f, -0.6f, -0.5f},  /* bottom-left */
		{0.0f, -0.7f, -0.7f},   /* bottom */
		{0.6f, -0.6f, -0.5f},   /* bottom-right */
	};
	int i = (int)light;

	if (i < 0 || i > 8)
		i = 0;
	*lx = dirs[i][0];
	*ly = dirs[i][1];
	*lz = dirs[i][2];
	vec_norm(lx, ly, lz);
}

bool
shitfetch_spin_parse_axis(const char *value, enum shitfetch_spin_axis *out)
{
	if (value == NULL)
		return false;
	if (strcmp(value, "x") == 0)
		*out = SHITFETCH_SPIN_AXIS_X;
	else if (strcmp(value, "y") == 0)
		*out = SHITFETCH_SPIN_AXIS_Y;
	else if (strcmp(value, "xy") == 0 || strcmp(value, "yx") == 0 || strcmp(value, "both") == 0)
		*out = SHITFETCH_SPIN_AXIS_XY;
	else
		return false;
	return true;
}

bool
shitfetch_spin_parse_light(const char *value, enum shitfetch_spin_light *out)
{
	static const struct {
		const char *name;
		enum shitfetch_spin_light light;
	} names[] = {
		{"top-left", SHITFETCH_SPIN_LIGHT_TOP_LEFT},
		{"top", SHITFETCH_SPIN_LIGHT_TOP},
		{"top-right", SHITFETCH_SPIN_LIGHT_TOP_RIGHT},
		{"left", SHITFETCH_SPIN_LIGHT_LEFT},
		{"front", SHITFETCH_SPIN_LIGHT_FRONT},
		{"right", SHITFETCH_SPIN_LIGHT_RIGHT},
		{"bottom-left", SHITFETCH_SPIN_LIGHT_BOTTOM_LEFT},
		{"bottom", SHITFETCH_SPIN_LIGHT_BOTTOM},
		{"bottom-right", SHITFETCH_SPIN_LIGHT_BOTTOM_RIGHT},
	};
	size_t i;

	if (value == NULL)
		return false;
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		if (strcmp(value, names[i].name) == 0) {
			*out = names[i].light;
			return true;
		}
	}
	return false;
}

bool
shitfetch_spin_parse_shade(const char *value, enum shitfetch_spin_shade *out)
{
	if (value == NULL)
		return false;
	if (strcmp(value, "auto") == 0)
		*out = SHITFETCH_SPIN_SHADE_AUTO;
	else if (strcmp(value, "ascii") == 0)
		*out = SHITFETCH_SPIN_SHADE_ASCII;
	else if (strcmp(value, "braille") == 0)
		*out = SHITFETCH_SPIN_SHADE_BRAILLE;
	else if (strcmp(value, "blocks") == 0)
		*out = SHITFETCH_SPIN_SHADE_BLOCKS;
	else
		return false;
	return true;
}

void
shitfetch_spin_clamp(struct shitfetch_spin *spin)
{
	if (spin->axis != SHITFETCH_SPIN_AXIS_X && spin->axis != SHITFETCH_SPIN_AXIS_Y)
		spin->axis = SHITFETCH_SPIN_AXIS_XY;
	if ((int)spin->light < 0 || (int)spin->light > 8)
		spin->light = SHITFETCH_SPIN_LIGHT_TOP_LEFT;
	spin->speed = clampf(spin->speed, 0.05f, 20.0f);
	spin->size = clampf(spin->size, 0.5f, 5.0f);
	if (spin->depth > 0.0f)
		spin->depth = clampf(spin->depth, 0.1f, 10.0f);
	else
		spin->depth = 0.0f;
	if (spin->height > 0) {
		if (spin->height < SPIN_MIN_ROWS)
			spin->height = SPIN_MIN_ROWS;
		if (spin->height > 200)
			spin->height = 200;
	} else {
		spin->height = 0;
	}
	if (spin->frames < 0)
		spin->frames = 0;
	if (spin->fps < 1)
		spin->fps = 1;
	if (spin->fps > 120)
		spin->fps = 120;
	if (spin->ramp[0] == '\0')
		snprintf(spin->ramp, sizeof(spin->ramp), "%s", SHITFETCH_SPIN_RAMP_DEFAULT);
}

static void
frame_free(struct spin_frame *f)
{
	free(f->ch);
	free(f->color);
	free(f->zbuf);
	memset(f, 0, sizeof(*f));
}

/* Reads the alphabet the art is drawn with and shades it in kind. Braille and block-element
   logos carry their whole shape in solid coverage, so re-rasterizing them on the matching
   subpixel grid keeps the silhouette they were designed for; art assembled from mixed ASCII
   punctuation already spells its own density out glyph by glyph, and the character ramp
   reproduces that better than a one-bit dither. Mixed art follows whichever family owns at
   least half the ink. */
static enum shitfetch_spin_shade
detect_shade(const struct shitfetch_logo_grid *grid)
{
	size_t total = 0;
	size_t braille = 0;
	size_t blocks = 0;
	size_t i;
	size_t n = grid->rows * grid->cols;

	for (i = 0; i < n; i++) {
		unsigned int cp = grid->cells[i].cp;

		if (cp == 0 || cp == ' ')
			continue;
		total++;
		if (cp >= 0x2800u && cp <= 0x28FFu)
			braille++;
		else if (cp >= 0x2580u && cp <= 0x259Fu)
			blocks++;
	}
	if (total == 0)
		return SHITFETCH_SPIN_SHADE_ASCII;
	if (braille * 2 >= total)
		return SHITFETCH_SPIN_SHADE_BRAILLE;
	if (blocks * 2 >= total)
		return SHITFETCH_SPIN_SHADE_BLOCKS;
	return SHITFETCH_SPIN_SHADE_ASCII;
}

/* Subpixels per cell for a shading target. Cells are about twice as tall as wide, so the 2x4
   braille grid is the only one whose subpixels come out square. */
static void
shade_subdivision(enum shitfetch_spin_shade shade, int *sub_x, int *sub_y)
{
	switch (shade) {
	case SHITFETCH_SPIN_SHADE_BRAILLE:
		*sub_x = 2;
		*sub_y = 4;
		return;
	case SHITFETCH_SPIN_SHADE_BLOCKS:
		*sub_x = 2;
		*sub_y = 2;
		return;
	case SHITFETCH_SPIN_SHADE_ASCII:
	default:
		*sub_x = 1;
		*sub_y = 1;
		return;
	}
}

static int
frame_alloc(struct spin_frame *f, int cols, int rows, enum shitfetch_spin_shade shade)
{
	int sub_x;
	int sub_y;
	size_t n;

	shade_subdivision(shade, &sub_x, &sub_y);
	n = (size_t)cols * (size_t)sub_x * (size_t)rows * (size_t)sub_y;

	frame_free(f);
	f->ch = malloc(n);
	f->color = malloc(n);
	f->zbuf = malloc(n * sizeof(*f->zbuf));
	if (f->ch == NULL || f->color == NULL || f->zbuf == NULL) {
		frame_free(f);
		return -1;
	}
	f->cols = cols;
	f->rows = rows;
	f->sub_x = sub_x;
	f->sub_y = sub_y;
	return 0;
}

/* Rotates the cloud, projects it with a z-buffer and shades each surviving sample. In ascii
   mode a sample is a whole cell holding a ramp character; in the subpixel modes it is one dot
   of the cell's grid holding a brightness for serialize() to turn into ink. Occupancy itself
   is never dithered, so a silhouette edge keeps the exact subpixel it landed on. */
static void
render_frame(struct spin_frame *f, const struct spin_model *model,
	const struct shitfetch_spin *spin, float angle_x, float angle_y, float cam, float k1)
{
	bool sub = f->sub_x > 1 || f->sub_y > 1;
	int width = f->cols * f->sub_x;
	int height = f->rows * f->sub_y;
	size_t n = (size_t)width * (size_t)height;
	size_t i;
	float sinx = sinf(angle_x);
	float cosx = cosf(angle_x);
	float siny = sinf(angle_y);
	float cosy = cosf(angle_y);
	float cx = (float)width * 0.5f;
	float cy = (float)height * 0.5f;
	float fx = (float)f->sub_x;
	float fy = (float)f->sub_y * 0.5f;
	float lx;
	float ly;
	float lz;
	float hx;
	float hy;
	float hz;
	size_t ramp_len = strlen(spin->ramp);

	if (ramp_len == 0)
		ramp_len = 1;
	light_vector(spin->light, &lx, &ly, &lz);
	hx = lx;
	hy = ly;
	hz = lz - 1.0f;
	vec_norm(&hx, &hy, &hz);

	memset(f->ch, ' ', n);
	memset(f->color, SPIN_EMPTY, n);
	for (i = 0; i < n; i++)
		f->zbuf[i] = 0.0f;

	for (i = 0; i < model->count; i++) {
		const struct spin_point *p = &model->points[i];
		float y1 = p->y * cosx - p->z * sinx;
		float z1 = p->y * sinx + p->z * cosx;
		float x2 = p->x * cosy + z1 * siny;
		float z2 = -p->x * siny + z1 * cosy;
		float ny1 = p->ny * cosx - p->nz * sinx;
		float nz1 = p->ny * sinx + p->nz * cosx;
		float nx2 = p->nx * cosy + nz1 * siny;
		float nz2 = -p->nx * siny + nz1 * cosy;
		float denom = z2 + cam;
		float ooz;
		float ndotl;
		float ndoth;
		float bright;
		char ink;
		int span_x;
		int span_y;
		int sx;
		int sy;
		int dx;
		int dy;

		if (denom <= 1.0f)
			continue;
		ooz = 1.0f / denom;
		sx = (int)(cx + fx * k1 * ooz * x2);
		sy = (int)(cy - fy * k1 * ooz * y1);

		ndotl = nx2 * lx + ny1 * ly + nz2 * lz;
		ndoth = nx2 * hx + ny1 * hy + nz2 * hz;
		if (ndotl < 0.0f)
			ndotl = 0.0f;
		if (ndoth < 0.0f)
			ndoth = 0.0f;
		bright = 0.15f + 0.65f * ndotl + 0.35f * powf(ndoth, SPIN_SPEC_POWER);
		bright = clampf(bright, 0.0f, 1.0f);
		ink = sub ? (char)(unsigned char)(bright * 255.0f)
			: spin->ramp[(size_t)(bright * (float)(ramp_len - 1))];

		/* One source cell covers k1*ooz cells on each axis, so a magnified cloud is splatted
		   over that footprint; without it the samples separate into holes. */
		span_x = (int)(k1 * ooz * fx + 0.999f);
		span_y = (int)(k1 * ooz * fy + 0.999f);
		if (span_x < 1)
			span_x = 1;
		if (span_y < 1)
			span_y = 1;
		if (span_x > SPIN_MAX_SPAN * f->sub_x)
			span_x = SPIN_MAX_SPAN * f->sub_x;
		if (span_y > SPIN_MAX_SPAN * f->sub_y)
			span_y = SPIN_MAX_SPAN * f->sub_y;
		sx -= (span_x - 1) / 2;
		sy -= (span_y - 1) / 2;
		for (dy = 0; dy < span_y; dy++) {
			int py = sy + dy;

			if (py < 0 || py >= height)
				continue;
			for (dx = 0; dx < span_x; dx++) {
				int px = sx + dx;
				size_t idx;

				if (px < 0 || px >= width)
					continue;
				idx = (size_t)py * (size_t)width + (size_t)px;
				if (ooz <= f->zbuf[idx])
					continue;
				f->zbuf[idx] = ooz;
				f->ch[idx] = ink;
				f->color[idx] = p->color_idx;
			}
		}
	}
}

static void
buf_put(char *out, size_t cap, size_t *o, const char *s, size_t n)
{
	if (*o >= cap)
		return;
	if (n > cap - *o)
		n = cap - *o;
	memcpy(out + *o, s, n);
	*o += n;
}

static void
buf_puts(char *out, size_t cap, size_t *o, const char *s)
{
	buf_put(out, cap, o, s, strlen(s));
}

static void
buf_putcp(char *out, size_t cap, size_t *o, unsigned int cp)
{
	char seq[4];
	size_t n;

	if (cp < 0x80u) {
		seq[0] = (char)cp;
		n = 1;
	} else if (cp < 0x800u) {
		seq[0] = (char)(0xC0u | (cp >> 6));
		seq[1] = (char)(0x80u | (cp & 0x3Fu));
		n = 2;
	} else if (cp < 0x10000u) {
		seq[0] = (char)(0xE0u | (cp >> 12));
		seq[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
		seq[2] = (char)(0x80u | (cp & 0x3Fu));
		n = 3;
	} else {
		seq[0] = (char)(0xF0u | (cp >> 18));
		seq[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
		seq[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
		seq[3] = (char)(0x80u | (cp & 0x3Fu));
		n = 4;
	}
	buf_put(out, cap, o, seq, n);
}

/* Collapses one cell's subpixel grid into a glyph and a colour. A cell the surface only
   partly covers becomes the braille or quadrant glyph for exactly those subpixels, which is
   what buys the subpixel modes their sharp silhouette. A fully covered cell has no edge to
   describe, so its glyph carries brightness instead: one of the four shade blocks, or for
   braille a cell-local threshold pattern, both of which read as an even texture rather than
   as noise. The colour comes from the nearest lit subpixel so a cell straddling two logo
   colours takes the one in front. Returns 0 when nothing in the cell is lit. */
static unsigned int
cell_glyph(const struct spin_frame *f, int row, int col, unsigned char *color_out)
{
	static const unsigned char braille_bit[8] = { 0, 3, 1, 4, 2, 5, 6, 7 };
	static const unsigned char braille_thr[8] = { 0, 4, 6, 2, 1, 5, 7, 3 };
	static const unsigned short blocks[16] = {
		0x0020, 0x2598, 0x259D, 0x2580, 0x2596, 0x258C, 0x259E, 0x259B,
		0x2597, 0x259A, 0x2590, 0x259C, 0x2584, 0x2599, 0x259F, 0x2588
	};
	static const unsigned short shades[4] = { 0x2591, 0x2592, 0x2593, 0x2588 };
	bool braille = f->sub_x == 2 && f->sub_y == 4;
	int width = f->cols * f->sub_x;
	int base_x = col * f->sub_x;
	int base_y = row * f->sub_y;
	unsigned int mask = 0;
	unsigned int sum = 0;
	int lit = 0;
	float best = 0.0f;
	float bright;
	int sx;
	int sy;
	int i;

	*color_out = SPIN_EMPTY;
	for (sy = 0; sy < f->sub_y; sy++) {
		for (sx = 0; sx < f->sub_x; sx++) {
			size_t idx = (size_t)(base_y + sy) * (size_t)width + (size_t)(base_x + sx);

			if (f->color[idx] == SPIN_EMPTY)
				continue;
			lit++;
			sum += (unsigned char)f->ch[idx];
			if (braille)
				mask |= 1u << braille_bit[sy * 2 + sx];
			else
				mask |= 1u << (sy * f->sub_x + sx);
			if (f->zbuf[idx] > best) {
				best = f->zbuf[idx];
				*color_out = f->color[idx];
			}
		}
	}
	if (lit == 0)
		return 0;
	if (f->sub_x == 1 && f->sub_y == 1)
		return (unsigned char)f->ch[(size_t)base_y * (size_t)width + (size_t)base_x];

	if (lit < f->sub_x * f->sub_y)
		return braille ? 0x2800u + mask : blocks[mask & 0xFu];

	bright = (float)sum / (float)lit / 255.0f;
	if (!braille)
		return shades[(int)clampf(bright * 4.0f, 0.0f, 3.0f)];
	/* Lifted off zero so the darkest interior still holds a couple of dots and the shape
	   does not develop holes where the light falls away. */
	bright = SPIN_DITHER_FLOOR + (1.0f - SPIN_DITHER_FLOOR) * bright;
	mask = 0;
	for (i = 0; i < 8; i++) {
		if ((float)braille_thr[i] / 8.0f < bright)
			mask |= 1u << braille_bit[i];
	}
	return 0x2800u + (mask != 0 ? mask : 1u);
}

static size_t
serialize(const struct spin_frame *f, const struct shitfetch_logo_grid *grid,
	const struct spin_side *side, int side_count, char *out, size_t cap, int rewind_rows)
{
	size_t o = 0;
	int row;

	if (rewind_rows > 1) {
		char seq[32];

		snprintf(seq, sizeof(seq), "\r\033[%dA", rewind_rows - 1);
		buf_puts(out, cap, &o, seq);
	} else if (rewind_rows == 1) {
		buf_put(out, cap, &o, "\r", 1);
	}
	for (row = 0; row < f->rows; row++) {
		unsigned char last = SPIN_EMPTY;
		int col;

		for (col = 0; col < f->cols; col++) {
			unsigned char c;
			unsigned int cp = cell_glyph(f, row, col, &c);

			if (cp == 0 || cp == ' ' || c == SPIN_EMPTY) {
				if (last != SPIN_EMPTY) {
					buf_puts(out, cap, &o, "\033[0m");
					last = SPIN_EMPTY;
				}
				buf_put(out, cap, &o, " ", 1);
				continue;
			}
			if (c != last) {
				char seq[48];

				snprintf(seq, sizeof(seq), "\033[%sm", grid->palette[c]);
				buf_puts(out, cap, &o, seq);
				last = c;
			}
			buf_putcp(out, cap, &o, cp);
		}
		if (last != SPIN_EMPTY)
			buf_puts(out, cap, &o, "\033[0m");
		if (row < side_count && side[row].text[0] != '\0') {
			buf_puts(out, cap, &o, "  ");
			buf_puts(out, cap, &o, side[row].text);
		}
		buf_puts(out, cap, &o, "\033[K");
		if (row + 1 < f->rows)
			buf_put(out, cap, &o, "\n", 1);
	}
	return o;
}

/* Mirrors the right-hand column of shitfetch_render: the user@host header, its dashed
   underline, the info lines verbatim, then the ANSI swatch rows after a blank line. */
static int
build_side(const struct shitfetch_settings *settings, const struct shitfetch_data *data,
	const char *key_color, struct shitfetch_info_line *info, struct spin_side *side, int cap)
{
	char user[128];
	char host[128];
	char dash[256];
	char header_color[32];
	char border_color[32];
	const char *effective_header = key_color;
	const char *effective_border = "38;5;244";
	size_t header_rows = settings->show_header ?
		(settings->template == SHITFETCH_TEMPLATE_MINI ? 1u : 2u) : 0u;
	size_t info_count;
	size_t dash_len;
	size_t i;
	size_t used = 0;
	bool has_colors_entry = false;

	for (i = 0; i < settings->entry_count; i++) {
		if (settings->entries[i].kind == SHITFETCH_ENTRY_COLORS && settings->entries[i].enabled) {
			has_colors_entry = true;
			break;
		}
	}
	if (settings->header_color_spec[0] != '\0' &&
		sfcolor_resolve(settings->header_color_spec, key_color, header_color, sizeof(header_color)))
		effective_header = header_color;
	if (settings->border_color_spec[0] != '\0' &&
		sfcolor_resolve(settings->border_color_spec, key_color, border_color, sizeof(border_color)))
		effective_border = border_color;

	shitfetch_identity(user, sizeof(user), host, sizeof(host));
	dash_len = strlen(user) + 1 + strlen(host);
	if (dash_len >= sizeof(dash))
		dash_len = sizeof(dash) - 1;
	memset(dash, '-', dash_len);
	dash[dash_len] = '\0';

	info_count = shitfetch_build_info_lines(settings, key_color, data, info, SHITFETCH_MAX_INFO_LINES);

	for (i = 0; i < header_rows && used < (size_t)cap; i++) {
		if (i == 0)
			snprintf(side[used].text, sizeof(side[used].text), "\033[1;%sm%s\033[0m@%s",
				effective_header, user, host);
		else
			snprintf(side[used].text, sizeof(side[used].text), "\033[%sm%s\033[0m",
				effective_border, dash);
		used++;
	}
	for (i = 0; i < info_count && used < (size_t)cap; i++)
		snprintf(side[used++].text, sizeof(side[used - 1].text), "%s", info[i].text);

	if (settings->show_ansi && !has_colors_entry) {
		int pass;

		if (used < (size_t)cap)
			side[used++].text[0] = '\0';
		for (pass = 0; pass < 2 && used < (size_t)cap; pass++) {
			char row[SHITFETCH_MAX_LINE];
			size_t o = 0;
			int j;

			for (j = 0; j < 8 && o < sizeof(row); j++)
				o += (size_t)snprintf(row + o, sizeof(row) - o, "\033[%dm   \033[0m",
					(pass == 0 ? 40 : 100) + j);
			snprintf(side[used++].text, sizeof(side[used - 1].text), "%s", row);
		}
	}
	return (int)used;
}

static int
side_width(const struct spin_side *side, int count)
{
	int width = 0;
	int i;

	for (i = 0; i < count; i++) {
		int w = (int)visible_width(side[i].text);

		if (w > width)
			width = w;
	}
	return width;
}

/* Fits the relief box to the current terminal and reallocates the frame plus the output
   buffer. want_cols/want_rows are the size the rotating cloud actually needs, so the box
   does not claim every free column and the info panel stays where the static layout puts
   it. A terminal too narrow to leave a usable relief width loses the panel instead. */
static int
relayout(struct spin_frame *frame, char **out, size_t *out_cap,
	const struct shitfetch_spin *spin, const struct spin_side *side, int side_count,
	int want_cols, int want_rows, int *visible_side)
{
	int tcols;
	int trows;
	int avail;
	int cols;
	int rows;
	int width;
	size_t cap;
	char *buf;

	term_size(&tcols, &trows);
	if (tcols < SPIN_MIN_COLS || trows < SPIN_MIN_ROWS)
		return -1;
	width = side_width(side, side_count);
	avail = width > 0 ? tcols - width - 2 : tcols;
	*visible_side = side_count;
	if (avail < SPIN_MIN_COLS) {
		avail = tcols;
		*visible_side = 0;
	}
	cols = want_cols < avail ? want_cols : avail;
	if (cols < SPIN_MIN_COLS)
		cols = avail < SPIN_MIN_COLS ? avail : SPIN_MIN_COLS;
	if (spin->height > 0) {
		rows = spin->height;
	} else {
		rows = side_count > want_rows ? side_count : want_rows;
		if (rows > trows - 1)
			rows = trows - 1;
	}
	if (rows > trows)
		rows = trows;
	if (rows < SPIN_MIN_ROWS)
		rows = SPIN_MIN_ROWS;
	if (frame_alloc(frame, cols, rows, spin->shade) != 0)
		return -1;
	cap = (size_t)rows * ((size_t)cols * 32u + sizeof(side[0].text) + 32u) + 64u;
	buf = realloc(*out, cap);
	if (buf == NULL)
		return -1;
	*out = buf;
	*out_cap = cap;
	return 0;
}

/* Largest projection scale that still keeps the cloud inside the relief box at its closest
   approach to the camera. Both extents are widened by the Z extent because rotation swaps
   depth into width and height; --spin-size is already folded into the box dimensions. */
static float
projection_scale(const struct spin_model *model, int cols, int rows, float cam)
{
	float wide = sqrtf(model->ext_x * model->ext_x + model->ext_z * model->ext_z);
	float tall = sqrtf(model->ext_y * model->ext_y + model->ext_z * model->ext_z);
	float front = cam - model->radius;
	float fit;

	if (wide < 1e-3f)
		wide = 1e-3f;
	if (tall < 1e-3f)
		tall = 1e-3f;
	if (front < 1.0f)
		front = 1.0f;
	fit = fminf((float)cols * 0.5f / wide, (float)rows / tall);
	return front * fit;
}

int
shitfetch_spin_run(const struct shitfetch_settings *settings, struct shitfetch_data *data,
	const char *key_color)
{
	const int side_cap = SHITFETCH_MAX_INFO_LINES + 5;
	struct shitfetch_logo_grid grid;
	struct shitfetch_spin spin;
	struct spin_model model;
	struct spin_frame frame;
	struct shitfetch_info_line *info;
	struct spin_side *side;
	char *out = NULL;
	size_t out_cap = 0;
	int side_count = 0;
	int visible_side = 0;
	int panel_w = 0;
	int want_cols = SPIN_MIN_COLS;
	int want_rows = SPIN_MIN_ROWS;
	long frame_no = 0;
	float angle_x = 0.0f;
	float angle_y = 0.0f;
	float cam;
	float k1 = 1.0f;
	double last;
	double tick;
	double target;
	bool ok = true;
	int tcols;
	int trows;

	term_size(&tcols, &trows);
	if (tcols < SPIN_MIN_COLS || trows < SPIN_MIN_ROWS)
		return -1;
	if (key_color == NULL || key_color[0] == '\0')
		key_color = "36";
	memset(&frame, 0, sizeof(frame));
	if (shitfetch_load_logo_grid(settings, data->os_id, &grid) != 0)
		return -1;
	spin = settings->spin;
	if (spin.shade == SHITFETCH_SPIN_SHADE_AUTO)
		spin.shade = detect_shade(&grid);
	if (build_model(&grid, &spin, &model) != 0) {
		shitfetch_logo_grid_free(&grid);
		return -1;
	}
	info = malloc(SHITFETCH_MAX_INFO_LINES * sizeof(*info));
	side = malloc((size_t)side_cap * sizeof(*side));
	if (info == NULL || side == NULL || !term_enter())
		ok = false;
	if (ok) {
		float wide = sqrtf(model.ext_x * model.ext_x + model.ext_z * model.ext_z);
		float tall = sqrtf(model.ext_y * model.ext_y + model.ext_z * model.ext_z);

		side_count = build_side(settings, data, key_color, info, side, side_cap);
		cam = model.radius * SPIN_CAM_MUL + SPIN_CAM_PAD;
		want_cols = (int)(2.0f * wide * spin.size) + 2;
		want_rows = (int)(tall * spin.size) + 1;
		target = 1.0 / (double)(spin.fps > 0 ? spin.fps : 30);
		last = now_seconds();
		tick = last;
		g_resized = 1;
		while (!g_stop) {
			double now;
			double dt;
			size_t len;

			if (g_resized) {
				g_resized = 0;
				block_erase();
				if (relayout(&frame, &out, &out_cap, &spin, side, side_count,
					want_cols, want_rows, &visible_side) != 0) {
					ok = false;
					break;
				}
				panel_w = side_width(side, visible_side);
				k1 = projection_scale(&model, frame.cols, frame.rows, cam);
			}
			render_frame(&frame, &model, &spin, angle_x, angle_y, cam, k1);
			len = serialize(&frame, &grid, side, visible_side, out, out_cap, g_block_rows);
			emit(out, len);
			g_block_rows = frame.rows;
			if (key_pressed())
				break;
			if (spin.frames > 0 && ++frame_no >= spin.frames)
				break;

			now = now_seconds();
			dt = now - last;
			if (dt <= 0.0 || dt > 0.25)
				dt = target;
			last = now;
			if (now - tick >= 1.0) {
				tick = now;
				shitfetch_refresh_live(settings, data);
				side_count = build_side(settings, data, key_color, info, side, side_cap);
				/* A longer uptime or a wider memory reading steals a column from the
				   relief, so the box has to be measured again. */
				if (side_width(side, visible_side) != panel_w)
					g_resized = 1;
			}
			if ((spin.axis & SHITFETCH_SPIN_AXIS_X) != 0)
				angle_x = fmodf(angle_x + spin.speed * (float)dt, 6.283185307f);
			if ((spin.axis & SHITFETCH_SPIN_AXIS_Y) != 0)
				angle_y = fmodf(angle_y + spin.speed * (float)dt, 6.283185307f);
			sleep_seconds(last + target - now_seconds());
		}
	}
	term_leave();
	frame_free(&frame);
	free(out);
	free(model.points);
	free(info);
	free(side);
	shitfetch_logo_grid_free(&grid);
	return ok ? 0 : -1;
}
