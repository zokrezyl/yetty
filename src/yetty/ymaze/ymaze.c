/*
 * ymaze.c — animated maze generator + solver, drawn as ypaint SDF prims.
 *
 * Port of yetty-poc/src/yetty/ydraw-maze/maze-renderer.cpp.
 */

#include <yetty/ymaze/ymaze.h>

#include <yetty/ypaint-core/buffer.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*=============================================================================
 * Wall-flag bits (cell holds an OR of N/S/E/W walls still standing).
 *===========================================================================*/

#define YMAZE_WALL_N 1u
#define YMAZE_WALL_S 2u
#define YMAZE_WALL_E 4u
#define YMAZE_WALL_W 8u

/*=============================================================================
 * Layout constants
 *===========================================================================*/

#define YMAZE_PAD 10.0f

/* Cap user input to sane sizes — same clamps the C++ argv parser used. */
#define YMAZE_COLS_MIN 3u
#define YMAZE_COLS_MAX 80u
#define YMAZE_ROWS_MIN 3u
#define YMAZE_ROWS_MAX 50u
#define YMAZE_SPEED_MIN 0.5f
#define YMAZE_SPEED_MAX 50.0f
#define YMAZE_WALL_MIN 0.3f
#define YMAZE_WALL_MAX 5.0f

/*=============================================================================
 * Path entry — (col, row) used in the BFS-derived solution.
 *===========================================================================*/

struct yetty_ymaze_cell_pos {
	uint32_t col;
	uint32_t row;
};

struct yetty_ymaze {
	struct yetty_ymaze_config config;

	/* Random state — held inline so we don't pull in a global. */
	uint64_t rng_state;

	/* cols * rows bytes; each cell holds the OR of its standing walls. */
	uint8_t *grid;
	uint32_t grid_len;

	/* BFS solution path from start to end (inclusive). */
	struct yetty_ymaze_cell_pos *path;
	uint32_t path_len;
	uint32_t path_cap;

	uint32_t start_col, start_row;
	uint32_t end_col,   end_row;

	float cell_w, cell_h;
	float origin_x, origin_y;

	float solve_start_time;
	bool  initialized;
};

/*=============================================================================
 * Tiny PRNG — splitmix64. Avoids dragging <random> equivalents in.
 *===========================================================================*/

static uint64_t ymaze_rng_next(struct yetty_ymaze *m)
{
	uint64_t z = (m->rng_state += 0x9E3779B97F4A7C15ULL);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

static uint32_t ymaze_rng_uint(struct yetty_ymaze *m, uint32_t bound)
{
	if (bound <= 1)
		return 0;
	return (uint32_t)(ymaze_rng_next(m) % bound);
}

static uint64_t ymaze_seed_from_clock(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	uint64_t s = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
	if (s == 0)
		s = 1;
	return s;
}

/*=============================================================================
 * Cell accessors — flat row-major grid.
 *===========================================================================*/

static inline uint8_t *ymaze_cell(struct yetty_ymaze *m, uint32_t col, uint32_t row)
{
	return &m->grid[row * m->config.cols + col];
}

static inline float ymaze_cell_x(const struct yetty_ymaze *m, uint32_t col)
{
	return m->origin_x + ((float)col + 0.5f) * m->cell_w;
}

static inline float ymaze_cell_y(const struct yetty_ymaze *m, uint32_t row)
{
	return m->origin_y + ((float)row + 0.5f) * m->cell_h;
}

/*=============================================================================
 * Path buffer growth (2x doubling, malloc-free on shrink/reset).
 *===========================================================================*/

static struct yetty_ycore_void_result ymaze_path_reset(struct yetty_ymaze *m, uint32_t hint)
{
	m->path_len = 0;
	if (hint > m->path_cap) {
		struct yetty_ymaze_cell_pos *np = realloc(m->path,
							  hint * sizeof(*np));
		if (!np)
			return YETTY_ERR(yetty_ycore_void,
					 "ymaze: path realloc failed");
		m->path = np;
		m->path_cap = hint;
	}
	return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ymaze_path_push(struct yetty_ymaze *m,
						      uint32_t col, uint32_t row)
{
	if (m->path_len == m->path_cap) {
		uint32_t nc = m->path_cap ? m->path_cap * 2 : 64;
		struct yetty_ymaze_cell_pos *np = realloc(m->path,
							  nc * sizeof(*np));
		if (!np)
			return YETTY_ERR(yetty_ycore_void,
					 "ymaze: path push realloc failed");
		m->path = np;
		m->path_cap = nc;
	}
	m->path[m->path_len].col = col;
	m->path[m->path_len].row = row;
	m->path_len++;
	return YETTY_OK_VOID();
}

/*=============================================================================
 * Grid layout — derived from cols/rows + scene size + padding.
 *===========================================================================*/

static void ymaze_layout(struct yetty_ymaze *m)
{
	float usable_w = m->config.scene_width  - 2.0f * YMAZE_PAD;
	float usable_h = m->config.scene_height - 2.0f * YMAZE_PAD;
	if (usable_w < 1.0f) usable_w = 1.0f;
	if (usable_h < 1.0f) usable_h = 1.0f;
	m->cell_w   = usable_w / (float)m->config.cols;
	m->cell_h   = usable_h / (float)m->config.rows;
	m->origin_x = YMAZE_PAD;
	m->origin_y = YMAZE_PAD;
}

/*=============================================================================
 * Maze generation — DFS with explicit stack (recursive backtracker).
 *===========================================================================*/

static struct yetty_ycore_void_result ymaze_generate_maze(struct yetty_ymaze *m)
{
	uint32_t total = m->config.cols * m->config.rows;

	if (total != m->grid_len) {
		uint8_t *ng = realloc(m->grid, total);
		if (!ng)
			return YETTY_ERR(yetty_ycore_void,
					 "ymaze: grid realloc failed");
		m->grid = ng;
		m->grid_len = total;
	}
	memset(m->grid, YMAZE_WALL_N | YMAZE_WALL_S | YMAZE_WALL_E | YMAZE_WALL_W,
	       total);

	ymaze_layout(m);

	m->start_col = 0;
	m->start_row = 0;
	m->end_col   = m->config.cols - 1;
	m->end_row   = m->config.rows - 1;

	uint8_t *visited = calloc(total, 1);
	struct yetty_ymaze_cell_pos *stack = malloc(total * sizeof(*stack));
	if (!visited || !stack) {
		free(visited);
		free(stack);
		return YETTY_ERR(yetty_ycore_void,
				 "ymaze: gen scratch alloc failed");
	}

	uint32_t sp = 0;
	visited[0] = 1;
	stack[sp++] = (struct yetty_ymaze_cell_pos){0, 0};

	const int dx[4]               = { 0,  0,  1, -1};
	const int dy[4]               = {-1,  1,  0,  0};
	const uint8_t wall_remove[4]  = {YMAZE_WALL_N, YMAZE_WALL_S,
					 YMAZE_WALL_E, YMAZE_WALL_W};
	const uint8_t wall_opposite[4] = {YMAZE_WALL_S, YMAZE_WALL_N,
					  YMAZE_WALL_W, YMAZE_WALL_E};

	while (sp > 0) {
		struct yetty_ymaze_cell_pos cur = stack[sp - 1];

		int neighbors[4];
		int ncount = 0;
		for (int d = 0; d < 4; d++) {
			int nx = (int)cur.col + dx[d];
			int ny = (int)cur.row + dy[d];
			if (nx < 0 || nx >= (int)m->config.cols ||
			    ny < 0 || ny >= (int)m->config.rows)
				continue;
			if (visited[(uint32_t)ny * m->config.cols + (uint32_t)nx])
				continue;
			neighbors[ncount++] = d;
		}

		if (ncount == 0) {
			sp--;
			continue;
		}

		int d = neighbors[ymaze_rng_uint(m, (uint32_t)ncount)];
		uint32_t nx = (uint32_t)((int)cur.col + dx[d]);
		uint32_t ny = (uint32_t)((int)cur.row + dy[d]);

		*ymaze_cell(m, cur.col, cur.row) &= (uint8_t)~wall_remove[d];
		*ymaze_cell(m, nx, ny)            &= (uint8_t)~wall_opposite[d];

		visited[ny * m->config.cols + nx] = 1;
		stack[sp++] = (struct yetty_ymaze_cell_pos){nx, ny};
	}

	free(visited);
	free(stack);
	return YETTY_OK_VOID();
}

/*=============================================================================
 * Solve — BFS from start to end, reconstruct via parent[] backtrace.
 *===========================================================================*/

static struct yetty_ycore_void_result ymaze_solve_maze(struct yetty_ymaze *m)
{
	uint32_t total = m->config.cols * m->config.rows;
	int32_t  *parent  = malloc(total * sizeof(int32_t));
	uint8_t  *visited = calloc(total, 1);
	uint32_t *queue   = malloc(total * sizeof(uint32_t));
	if (!parent || !visited || !queue) {
		free(parent); free(visited); free(queue);
		return YETTY_ERR(yetty_ycore_void,
				 "ymaze: solve scratch alloc failed");
	}
	for (uint32_t i = 0; i < total; i++)
		parent[i] = -1;

	uint32_t start_idx = m->start_row * m->config.cols + m->start_col;
	uint32_t end_idx   = m->end_row   * m->config.cols + m->end_col;

	uint32_t qhead = 0, qtail = 0;
	visited[start_idx] = 1;
	queue[qtail++] = start_idx;

	const int dx[4]              = { 0,  0,  1, -1};
	const int dy[4]              = {-1,  1,  0,  0};
	const uint8_t wall_check[4]  = {YMAZE_WALL_N, YMAZE_WALL_S,
					YMAZE_WALL_E, YMAZE_WALL_W};

	while (qhead < qtail) {
		uint32_t idx = queue[qhead++];
		if (idx == end_idx)
			break;

		uint32_t cx = idx % m->config.cols;
		uint32_t cy = idx / m->config.cols;

		for (int d = 0; d < 4; d++) {
			if (m->grid[idx] & wall_check[d])
				continue;

			int nx = (int)cx + dx[d];
			int ny = (int)cy + dy[d];
			if (nx < 0 || nx >= (int)m->config.cols ||
			    ny < 0 || ny >= (int)m->config.rows)
				continue;

			uint32_t n_idx = (uint32_t)ny * m->config.cols
					+ (uint32_t)nx;
			if (visited[n_idx])
				continue;
			visited[n_idx] = 1;
			parent[n_idx] = (int32_t)idx;
			queue[qtail++] = n_idx;
		}
	}

	struct yetty_ycore_void_result rr = ymaze_path_reset(m, total);
	if (YETTY_IS_ERR(rr)) {
		free(parent); free(visited); free(queue);
		return rr;
	}

	int32_t cur = (int32_t)end_idx;
	while (cur >= 0) {
		uint32_t cx = (uint32_t)cur % m->config.cols;
		uint32_t cy = (uint32_t)cur / m->config.cols;
		struct yetty_ycore_void_result pp = ymaze_path_push(m, cx, cy);
		if (YETTY_IS_ERR(pp)) {
			free(parent); free(visited); free(queue);
			return pp;
		}
		cur = parent[cur];
	}

	free(parent); free(visited); free(queue);

	/* path was filled end → start; reverse in-place. */
	for (uint32_t i = 0, j = m->path_len; i + 1 < j; i++, j--) {
		struct yetty_ymaze_cell_pos t = m->path[i];
		m->path[i] = m->path[j - 1];
		m->path[j - 1] = t;
	}
	return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ymaze_generate(struct yetty_ymaze *m)
{
	struct yetty_ycore_void_result g = ymaze_generate_maze(m);
	YETTY_RETURN_IF_ERR(yetty_ycore_void, g, "ymaze: generate_maze failed");
	struct yetty_ycore_void_result s = ymaze_solve_maze(m);
	YETTY_RETURN_IF_ERR(yetty_ycore_void, s, "ymaze: solve_maze failed");
	return YETTY_OK_VOID();
}

/*=============================================================================
 * Primitive emission — walls, start/end markers, trail, actor.
 *
 * z_order rises monotonically per prim so later prims sit on top.
 *===========================================================================*/

static struct yetty_ycore_void_result ymaze_build_prims(struct yetty_ymaze *m,
							struct yetty_ypaint_core_buffer *buf,
							float time)
{
	uint32_t z = 0;

	/* Walls — emit S+E for every cell, plus N for row 0 and W for col 0,
	 * which avoids drawing each shared wall twice. Segments have no
	 * interior; the visible band comes from the stroke (stroke_color +
	 * stroke_width), not the fill, so wall_color goes into the stroke
	 * slot. */
	for (uint32_t row = 0; row < m->config.rows; row++) {
		for (uint32_t col = 0; col < m->config.cols; col++) {
			uint8_t w = *ymaze_cell(m, col, row);
			float x0 = m->origin_x + (float)col * m->cell_w;
			float y0 = m->origin_y + (float)row * m->cell_h;
			float x1 = x0 + m->cell_w;
			float y1 = y0 + m->cell_h;

			struct yetty_ysdf_segment seg;

			if ((w & YMAZE_WALL_N) && row == 0) {
				seg = (struct yetty_ysdf_segment){x0, y0, x1, y0};
				yetty_ysdf_add_segment(buf, z++, 0u,
						       m->config.wall_color,
						       m->config.wall_width, &seg);
			}
			if (w & YMAZE_WALL_S) {
				seg = (struct yetty_ysdf_segment){x0, y1, x1, y1};
				yetty_ysdf_add_segment(buf, z++, 0u,
						       m->config.wall_color,
						       m->config.wall_width, &seg);
			}
			if ((w & YMAZE_WALL_W) && col == 0) {
				seg = (struct yetty_ysdf_segment){x0, y0, x0, y1};
				yetty_ysdf_add_segment(buf, z++, 0u,
						       m->config.wall_color,
						       m->config.wall_width, &seg);
			}
			if (w & YMAZE_WALL_E) {
				seg = (struct yetty_ysdf_segment){x1, y0, x1, y1};
				yetty_ysdf_add_segment(buf, z++, 0u,
						       m->config.wall_color,
						       m->config.wall_width, &seg);
			}
		}
	}

	float cell_min = m->cell_w < m->cell_h ? m->cell_w : m->cell_h;

	/* Start marker — circle. */
	{
		struct yetty_ysdf_circle c = {
			.center_x = ymaze_cell_x(m, m->start_col),
			.center_y = ymaze_cell_y(m, m->start_row),
			.radius   = cell_min * 0.3f,
		};
		yetty_ysdf_add_circle(buf, z++, m->config.start_color, 0u, 0.0f, &c);
	}

	/* End marker — 5-point star. */
	{
		struct yetty_ysdf_star s = {
			.center_x    = ymaze_cell_x(m, m->end_col),
			.center_y    = ymaze_cell_y(m, m->end_row),
			.radius      = cell_min * 0.35f,
			.num_points  = 5.0f,
			.inner_ratio = 2.2f,
		};
		yetty_ysdf_add_star(buf, z++, m->config.end_color, 0u, 0.0f, &s);
	}

	if (m->path_len == 0)
		return YETTY_OK_VOID();

	float elapsed = time - m->solve_start_time;
	if (elapsed < 0.0f) elapsed = 0.0f;

	/* Trail — segments along path[0..cells_traversed]. */
	{
		float total_cells_f = elapsed * m->config.actor_speed;
		uint32_t cells_traversed =
			total_cells_f < 0.0f ? 0u : (uint32_t)total_cells_f;
		if (cells_traversed > m->path_len - 1)
			cells_traversed = m->path_len - 1;

		uint32_t trail_color = (m->config.actor_color & 0x00FFFFFFu)
				     | 0x60000000u;
		float trail_width = cell_min * 0.15f;
		for (uint32_t i = 0; i < cells_traversed && i + 1 < m->path_len; i++) {
			struct yetty_ysdf_segment seg = {
				.start_x = ymaze_cell_x(m, m->path[i].col),
				.start_y = ymaze_cell_y(m, m->path[i].row),
				.end_x   = ymaze_cell_x(m, m->path[i + 1].col),
				.end_y   = ymaze_cell_y(m, m->path[i + 1].row),
			};
			/* Segment has no interior — paint via stroke. */
			yetty_ysdf_add_segment(buf, z++, 0u, trail_color,
					       trail_width, &seg);
		}
	}

	/* Actor — circle interpolated between path[idx] and path[idx + 1]. */
	{
		float progress = elapsed * m->config.actor_speed;
		uint32_t idx = progress < 0.0f ? 0u : (uint32_t)progress;
		float frac = progress - (float)idx;

		float ax, ay;
		if (idx >= m->path_len - 1) {
			ax = ymaze_cell_x(m, m->path[m->path_len - 1].col);
			ay = ymaze_cell_y(m, m->path[m->path_len - 1].row);
		} else {
			float x0 = ymaze_cell_x(m, m->path[idx].col);
			float y0 = ymaze_cell_y(m, m->path[idx].row);
			float x1 = ymaze_cell_x(m, m->path[idx + 1].col);
			float y1 = ymaze_cell_y(m, m->path[idx + 1].row);
			ax = x0 + (x1 - x0) * frac;
			ay = y0 + (y1 - y0) * frac;
		}

		struct yetty_ysdf_circle c = {
			.center_x = ax,
			.center_y = ay,
			.radius   = cell_min * 0.28f,
		};
		yetty_ysdf_add_circle(buf, z++, m->config.actor_color, 0u, 0.0f, &c);
	}

	return YETTY_OK_VOID();
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_ymaze_config yetty_ymaze_config_default(void)
{
	struct yetty_ymaze_config cfg = {
		.cols         = 15,
		.rows         = 10,
		.actor_speed  = 4.0f,
		.wall_width   = 1.5f,
		.wall_color   = 0xFF808080u,
		.actor_color  = 0xFF00CCFFu,
		.start_color  = 0xFF00FF00u,
		.end_color    = 0xFF0000FFu,
		.bg_color     = 0xFF1A1A2Eu,
		.scene_width  = 600.0f,
		.scene_height = 400.0f,
		.auto_regen   = true,
	};
	return cfg;
}

static struct yetty_ymaze_config ymaze_clamp_config(struct yetty_ymaze_config c)
{
	if (c.cols < YMAZE_COLS_MIN) c.cols = YMAZE_COLS_MIN;
	if (c.cols > YMAZE_COLS_MAX) c.cols = YMAZE_COLS_MAX;
	if (c.rows < YMAZE_ROWS_MIN) c.rows = YMAZE_ROWS_MIN;
	if (c.rows > YMAZE_ROWS_MAX) c.rows = YMAZE_ROWS_MAX;
	if (c.actor_speed < YMAZE_SPEED_MIN) c.actor_speed = YMAZE_SPEED_MIN;
	if (c.actor_speed > YMAZE_SPEED_MAX) c.actor_speed = YMAZE_SPEED_MAX;
	if (c.wall_width < YMAZE_WALL_MIN) c.wall_width = YMAZE_WALL_MIN;
	if (c.wall_width > YMAZE_WALL_MAX) c.wall_width = YMAZE_WALL_MAX;
	if (c.scene_width  < 1.0f) c.scene_width  = 1.0f;
	if (c.scene_height < 1.0f) c.scene_height = 1.0f;
	return c;
}

struct yetty_ymaze_ptr_result yetty_ymaze_create(const struct yetty_ymaze_config *config,
						 uint32_t seed)
{
	struct yetty_ymaze *m = calloc(1, sizeof(struct yetty_ymaze));
	if (!m)
		return YETTY_ERR(yetty_ymaze_ptr, "ymaze: calloc failed");

	m->config     = ymaze_clamp_config(config ? *config
					   : yetty_ymaze_config_default());
	m->rng_state  = seed != 0 ? (uint64_t)seed : ymaze_seed_from_clock();
	m->initialized = false;
	return YETTY_OK(yetty_ymaze_ptr, m);
}

void yetty_ymaze_destroy(struct yetty_ymaze *maze)
{
	if (!maze)
		return;
	free(maze->grid);
	free(maze->path);
	free(maze);
}

struct yetty_ycore_void_result yetty_ymaze_set_scene_size(struct yetty_ymaze *maze,
							  float scene_width, float scene_height)
{
	if (!maze)
		return YETTY_ERR(yetty_ycore_void, "ymaze: maze is NULL");
	if (scene_width  < 1.0f) scene_width  = 1.0f;
	if (scene_height < 1.0f) scene_height = 1.0f;
	maze->config.scene_width  = scene_width;
	maze->config.scene_height = scene_height;
	ymaze_layout(maze);
	return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ymaze_regenerate(struct yetty_ymaze *maze)
{
	if (!maze)
		return YETTY_ERR(yetty_ycore_void, "ymaze: maze is NULL");
	maze->initialized = false;
	return YETTY_OK_VOID();
}

bool yetty_ymaze_is_finished(const struct yetty_ymaze *maze, float time)
{
	if (!maze || maze->path_len == 0)
		return false;
	float elapsed = time - maze->solve_start_time;
	float total_time = (float)(maze->path_len - 1) / maze->config.actor_speed;
	return elapsed > total_time + 1.0f;
}

const struct yetty_ymaze_config *yetty_ymaze_config_get(const struct yetty_ymaze *maze)
{
	return maze ? &maze->config : NULL;
}

struct yetty_ycore_void_result yetty_ymaze_render(struct yetty_ymaze *maze,
						  struct yetty_ypaint_core_buffer *buf,
						  float time, bool *out_regenerated)
{
	if (!maze)
		return YETTY_ERR(yetty_ycore_void, "ymaze_render: maze is NULL");
	if (!buf)
		return YETTY_ERR(yetty_ycore_void, "ymaze_render: buf is NULL");

	bool regenerated = false;

	if (!maze->initialized) {
		struct yetty_ycore_void_result gr = ymaze_generate(maze);
		YETTY_RETURN_IF_ERR(yetty_ycore_void, gr,
				    "ymaze_render: initial generate failed");
		maze->solve_start_time = time;
		maze->initialized = true;
		regenerated = true;
	}

	if (maze->config.auto_regen && maze->path_len > 0
	    && yetty_ymaze_is_finished(maze, time)) {
		struct yetty_ycore_void_result gr = ymaze_generate(maze);
		YETTY_RETURN_IF_ERR(yetty_ycore_void, gr,
				    "ymaze_render: auto-regen failed");
		maze->solve_start_time = time;
		regenerated = true;
	}

	yetty_ypaint_core_buffer_clear(buf);
	yetty_ypaint_core_buffer_set_scene_bounds(buf, 0.0f, 0.0f,
						  maze->config.scene_width,
						  maze->config.scene_height);

	struct yetty_ycore_void_result br = ymaze_build_prims(maze, buf, time);
	YETTY_RETURN_IF_ERR(yetty_ycore_void, br,
			    "ymaze_render: build_prims failed");

	if (out_regenerated)
		*out_regenerated = regenerated;
	return YETTY_OK_VOID();
}
