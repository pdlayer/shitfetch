#define _POSIX_C_SOURCE 200809L

#include "sfdetect.h"
#include "sfdetectrpm.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* rpm stores its database in one of three backends. All of them are read
 * directly here so that neither librpm nor libsqlite3 is needed:
 *
 *   rpmdb.sqlite  sqlite backend, default since rpm 4.16 (Fedora, RHEL 9+)
 *   Packages.db   ndb backend (openSUSE, SLE)
 *   Packages      legacy Berkeley DB hash file (CentOS 7/8, old Fedora)
 */

#define RPM_NDB_MAGIC 0x506d7052u
#define RPM_NDB_SLOT_MAGIC 0x746f6c53u
#define RPM_NDB_PAGE_SIZE 4096
#define RPM_NDB_HEADER_SIZE 32
#define RPM_NDB_SLOT_SIZE 16
#define RPM_NDB_SLOT_START 2
#define RPM_NDB_OFFSET_SLOTNPAGES 12
#define RPM_NDB_MAX_SLOT_PAGES 4096

#define RPM_BDB_HASH_MAGIC 0x00061561u
#define RPM_BDB_META_SIZE 512
#define RPM_BDB_OFFSET_MAGIC 12
#define RPM_BDB_OFFSET_TYPE 25
#define RPM_BDB_OFFSET_NELEM 88
#define RPM_BDB_TYPE_HASHMETA 8
#define RPM_BDB_MAX_NELEM 2000000u

#define RPM_SQLITE_HEADER_SIZE 100
#define RPM_SQLITE_MIN_PAGE_SIZE 512
#define RPM_SQLITE_MAX_PAGE_SIZE 65536
#define RPM_SQLITE_MIN_USABLE_SIZE 480
#define RPM_SQLITE_INTERIOR_TABLE 0x05
#define RPM_SQLITE_LEAF_TABLE 0x0d
#define RPM_SQLITE_MASTER_COLUMNS 4
#define RPM_SQLITE_MAX_DEPTH 24
#define RPM_SQLITE_PAGE_BUDGET 200000L

static uint32_t
rpm_le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t
rpm_be32(const unsigned char *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
		((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t
rpm_be16(const unsigned char *p)
{
	return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
}

static bool
rpm_read_at(FILE *fp, long offset, unsigned char *buf, size_t len)
{
	if (offset < 0 || fseek(fp, offset, SEEK_SET) != 0)
		return false;
	return fread(buf, 1, len, fp) == len;
}

static long
rpm_file_size(FILE *fp)
{
	if (fseek(fp, 0, SEEK_END) != 0)
		return -1;
	return ftell(fp);
}

/* ndb: a 32 byte header followed by slot pages, one 16 byte slot per
 * package header. A slot with a zero block offset is a free slot. */
static int
rpm_count_ndb(const char *path)
{
	FILE *fp;
	unsigned char header[RPM_NDB_HEADER_SIZE];
	unsigned char page[RPM_NDB_PAGE_SIZE];
	uint32_t slot_pages;
	uint32_t page_index;
	uint32_t offset;
	int count = 0;

	fp = fopen(path, "rb");
	if (fp == NULL)
		return -1;
	if (!rpm_read_at(fp, 0, header, sizeof(header)) ||
		rpm_le32(header) != RPM_NDB_MAGIC) {
		fclose(fp);
		return -1;
	}
	slot_pages = rpm_le32(header + RPM_NDB_OFFSET_SLOTNPAGES);
	if (slot_pages == 0 || slot_pages > RPM_NDB_MAX_SLOT_PAGES) {
		fclose(fp);
		return -1;
	}
	for (page_index = 0; page_index < slot_pages; page_index++) {
		if (!rpm_read_at(fp, (long)page_index * RPM_NDB_PAGE_SIZE, page, sizeof(page)))
			break;
		offset = page_index == 0 ? RPM_NDB_SLOT_START * RPM_NDB_SLOT_SIZE : 0;
		for (; offset + RPM_NDB_SLOT_SIZE <= RPM_NDB_PAGE_SIZE; offset += RPM_NDB_SLOT_SIZE) {
			const unsigned char *slot = page + offset;

			if (rpm_le32(slot) != RPM_NDB_SLOT_MAGIC) {
				fclose(fp);
				return count > 0 ? count : -1;
			}
			if (rpm_le32(slot + 8) == 0 || rpm_le32(slot + 4) == 0)
				continue;
			count++;
		}
	}
	fclose(fp);
	return count > 0 ? count : -1;
}

/* Berkeley DB hash: the metadata page caches the number of keys. rpm keeps
 * one extra record under key 0 holding the next header instance number. */
static int
rpm_count_bdb(const char *path)
{
	FILE *fp;
	unsigned char meta[RPM_BDB_META_SIZE];
	uint32_t nelem;
	bool swapped;

	fp = fopen(path, "rb");
	if (fp == NULL)
		return -1;
	if (!rpm_read_at(fp, 0, meta, sizeof(meta))) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	if (rpm_le32(meta + RPM_BDB_OFFSET_MAGIC) == RPM_BDB_HASH_MAGIC)
		swapped = false;
	else if (rpm_be32(meta + RPM_BDB_OFFSET_MAGIC) == RPM_BDB_HASH_MAGIC)
		swapped = true;
	else
		return -1;
	if (meta[RPM_BDB_OFFSET_TYPE] != RPM_BDB_TYPE_HASHMETA)
		return -1;
	nelem = swapped ? rpm_be32(meta + RPM_BDB_OFFSET_NELEM)
			: rpm_le32(meta + RPM_BDB_OFFSET_NELEM);
	if (nelem < 2 || nelem > RPM_BDB_MAX_NELEM)
		return -1;
	return (int)(nelem - 1);
}

/* sqlite: walk the file format by hand. The rpm schema stores one row per
 * package header in the table "Packages", so the row count is the package
 * count. Rows written to a not yet checkpointed WAL are not seen. */
struct rpm_sqlite {
	FILE *fp;
	uint32_t page_size;
	uint32_t usable_size;
	uint32_t page_count;
	long budget;
};

static bool
rpm_sqlite_read_page(struct rpm_sqlite *db, uint32_t pgno, unsigned char *page)
{
	if (pgno == 0 || pgno > db->page_count || db->budget <= 0)
		return false;
	db->budget--;
	return rpm_read_at(db->fp, (long)(pgno - 1) * (long)db->page_size, page, db->page_size);
}

static const unsigned char *
rpm_sqlite_varint(const unsigned char *p, const unsigned char *end, uint64_t *out)
{
	uint64_t value = 0;
	int i;

	for (i = 0; i < 8; i++) {
		if (p >= end)
			return NULL;
		value = (value << 7) | (uint64_t)(*p & 0x7f);
		if ((*p++ & 0x80) == 0) {
			*out = value;
			return p;
		}
	}
	if (p >= end)
		return NULL;
	value = (value << 8) | (uint64_t)*p++;
	*out = value;
	return p;
}

static uint64_t
rpm_sqlite_serial_size(uint64_t serial)
{
	static const uint64_t fixed[10] = {0, 1, 2, 3, 4, 6, 8, 8, 0, 0};

	if (serial < 10)
		return fixed[serial];
	if (serial < 12)
		return 0;
	return (serial - 12) / 2;
}

static uint64_t
rpm_sqlite_read_int(const unsigned char *p, size_t len)
{
	uint64_t value = 0;
	size_t i;

	for (i = 0; i < len; i++)
		value = (value << 8) | (uint64_t)p[i];
	return value;
}

static bool
rpm_sqlite_is_text(uint64_t serial)
{
	return serial >= 13 && (serial & 1) != 0;
}

static uint32_t
rpm_sqlite_max_cells(const struct rpm_sqlite *db, uint32_t header_offset, uint32_t cell_header)
{
	if (db->usable_size <= header_offset + cell_header)
		return 0;
	return (db->usable_size - header_offset - cell_header) / 2;
}

/* Decode one sqlite_master record: type, name, tbl_name, rootpage. Returns
 * the root page when the row describes the wanted table, otherwise 0. */
static uint32_t
rpm_sqlite_master_root(const unsigned char *record, const unsigned char *end, const char *table)
{
	uint64_t serial[RPM_SQLITE_MASTER_COLUMNS];
	const unsigned char *types;
	const unsigned char *value;
	uint64_t header_size;
	uint64_t size;
	size_t name_len = strlen(table);
	size_t i;

	types = rpm_sqlite_varint(record, end, &header_size);
	if (types == NULL || header_size < 1 || (uint64_t)(end - record) < header_size)
		return 0;
	for (i = 0; i < RPM_SQLITE_MASTER_COLUMNS; i++) {
		types = rpm_sqlite_varint(types, record + header_size, &serial[i]);
		if (types == NULL)
			return 0;
	}
	value = record + header_size;
	for (i = 0; i < RPM_SQLITE_MASTER_COLUMNS; i++) {
		size = rpm_sqlite_serial_size(serial[i]);
		if ((uint64_t)(end - value) < size)
			return 0;
		if (i == 0 && (!rpm_sqlite_is_text(serial[i]) || size != 5 ||
			memcmp(value, "table", 5) != 0))
			return 0;
		if (i == 1 && (!rpm_sqlite_is_text(serial[i]) || size != name_len ||
			memcmp(value, table, name_len) != 0))
			return 0;
		if (i == 3) {
			if (serial[i] < 1 || serial[i] > 6)
				return 0;
			return (uint32_t)rpm_sqlite_read_int(value, (size_t)size);
		}
		value += size;
	}
	return 0;
}

static uint32_t
rpm_sqlite_find_table(struct rpm_sqlite *db, uint32_t pgno, const char *table, int depth)
{
	unsigned char *page;
	const unsigned char *end;
	const unsigned char *p;
	uint32_t header_offset = pgno == 1 ? RPM_SQLITE_HEADER_SIZE : 0;
	uint32_t cells;
	uint32_t cell_offset;
	uint32_t root = 0;
	uint32_t i;
	uint64_t payload;
	uint64_t rowid;
	int type;

	if (depth > RPM_SQLITE_MAX_DEPTH)
		return 0;
	page = malloc(db->page_size);
	if (page == NULL)
		return 0;
	if (!rpm_sqlite_read_page(db, pgno, page)) {
		free(page);
		return 0;
	}
	end = page + db->usable_size;
	type = page[header_offset];
	cells = rpm_be16(page + header_offset + 3);
	if (type == RPM_SQLITE_LEAF_TABLE && cells <= rpm_sqlite_max_cells(db, header_offset, 8)) {
		for (i = 0; i < cells && root == 0; i++) {
			cell_offset = rpm_be16(page + header_offset + 8 + i * 2);
			if (cell_offset >= db->usable_size)
				continue;
			p = rpm_sqlite_varint(page + cell_offset, end, &payload);
			if (p != NULL)
				p = rpm_sqlite_varint(p, end, &rowid);
			if (p != NULL)
				root = rpm_sqlite_master_root(p, end, table);
		}
	} else if (type == RPM_SQLITE_INTERIOR_TABLE && cells <= rpm_sqlite_max_cells(db, header_offset, 12)) {
		for (i = 0; i < cells && root == 0; i++) {
			cell_offset = rpm_be16(page + header_offset + 12 + i * 2);
			if (cell_offset + 4 > db->usable_size)
				continue;
			root = rpm_sqlite_find_table(db, rpm_be32(page + cell_offset), table, depth + 1);
		}
		if (root == 0)
			root = rpm_sqlite_find_table(db, rpm_be32(page + header_offset + 8), table, depth + 1);
	}
	free(page);
	return root;
}

static long
rpm_sqlite_count_rows(struct rpm_sqlite *db, uint32_t pgno, int depth)
{
	unsigned char *page;
	uint32_t header_offset = pgno == 1 ? RPM_SQLITE_HEADER_SIZE : 0;
	uint32_t right_pgno;
	uint32_t cell_offset;
	uint32_t cells;
	uint32_t i;
	long total = 0;
	long sub;
	int type;

	if (depth > RPM_SQLITE_MAX_DEPTH)
		return -1;
	page = malloc(db->page_size);
	if (page == NULL)
		return -1;
	if (!rpm_sqlite_read_page(db, pgno, page)) {
		free(page);
		return -1;
	}
	type = page[header_offset];
	cells = rpm_be16(page + header_offset + 3);
	if (type == RPM_SQLITE_LEAF_TABLE) {
		free(page);
		return cells <= rpm_sqlite_max_cells(db, header_offset, 8) ? (long)cells : -1;
	}
	if (type != RPM_SQLITE_INTERIOR_TABLE ||
		cells > rpm_sqlite_max_cells(db, header_offset, 12)) {
		free(page);
		return -1;
	}
	for (i = 0; i < cells; i++) {
		cell_offset = rpm_be16(page + header_offset + 12 + i * 2);
		if (cell_offset + 4 > db->usable_size) {
			free(page);
			return -1;
		}
		sub = rpm_sqlite_count_rows(db, rpm_be32(page + cell_offset), depth + 1);
		if (sub < 0) {
			free(page);
			return -1;
		}
		total += sub;
	}
	right_pgno = rpm_be32(page + header_offset + 8);
	free(page);
	sub = rpm_sqlite_count_rows(db, right_pgno, depth + 1);
	return sub < 0 ? -1 : total + sub;
}

static int
rpm_count_sqlite(const char *path)
{
	static const char magic[16] = "SQLite format 3";
	struct rpm_sqlite db;
	unsigned char header[RPM_SQLITE_HEADER_SIZE];
	uint32_t reserved;
	uint32_t root;
	long size;
	long rows;

	db.fp = fopen(path, "rb");
	if (db.fp == NULL)
		return -1;
	size = rpm_file_size(db.fp);
	if (!rpm_read_at(db.fp, 0, header, sizeof(header)) ||
		memcmp(header, magic, sizeof(magic)) != 0) {
		fclose(db.fp);
		return -1;
	}
	db.page_size = rpm_be16(header + 16);
	if (db.page_size == 1)
		db.page_size = RPM_SQLITE_MAX_PAGE_SIZE;
	reserved = header[20];
	if (db.page_size < RPM_SQLITE_MIN_PAGE_SIZE ||
		(db.page_size & (db.page_size - 1)) != 0 ||
		reserved >= db.page_size ||
		size < (long)db.page_size) {
		fclose(db.fp);
		return -1;
	}
	db.usable_size = db.page_size - reserved;
	if (db.usable_size < RPM_SQLITE_MIN_USABLE_SIZE) {
		fclose(db.fp);
		return -1;
	}
	db.page_count = (uint32_t)((unsigned long)size / db.page_size);
	db.budget = RPM_SQLITE_PAGE_BUDGET;
	root = rpm_sqlite_find_table(&db, 1, "Packages", 0);
	rows = root == 0 ? -1 : rpm_sqlite_count_rows(&db, root, 0);
	fclose(db.fp);
	if (rows <= 0 || rows > INT_MAX)
		return -1;
	return (int)rows;
}

int
shitfetch_count_rpm_dir(const char *dir)
{
	char path[SHITFETCH_MAX_PATH];
	int count;

	if (dir == NULL || dir[0] == '\0')
		return -1;
	snprintf(path, sizeof(path), "%s/rpmdb.sqlite", dir);
	count = rpm_count_sqlite(path);
	if (count > 0)
		return count;
	snprintf(path, sizeof(path), "%s/Packages.db", dir);
	count = rpm_count_ndb(path);
	if (count > 0)
		return count;
	snprintf(path, sizeof(path), "%s/Packages", dir);
	return rpm_count_bdb(path);
}

int
shitfetch_count_rpm(void)
{
	int count = shitfetch_count_rpm_dir("/usr/lib/sysimage/rpm");

	if (count > 0)
		return count;
	return shitfetch_count_rpm_dir("/var/lib/rpm");
}
