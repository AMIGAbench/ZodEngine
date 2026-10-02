/*
 * Prueft die Serialisierungsschicht (port/zod_wire.cpp):
 *  - Little-Endian-Primitive gegen feste Bytemuster
 *  - Hin- und Rueckwandlung aller Pakete mit Tabelleneintrag
 *  - erwartete Bytefolge fuer ausgewaehlte Pakete im Swaptest-Build
 *
 * Laeuft in beiden Varianten: normal (x86, Tausch = Nichtstun) und mit
 * -DZOD_WIRE_SWAPTEST (simuliert die Big-Endian-Seite).
 */
#include <cstdio>
#include <cstring>
#include <string>

#include <zod_wire.h>
#include <lib_qZod_DnSeparate/event_handler.h>
#include <lib_qZod_DnObjects/zod_obj_structures.h>

static int failures = 0;

static void check(bool ok, const std::string &what)
{
	if(!ok)
	{
		printf("  FEHLER: %s\n", what.c_str());
		failures++;
	}
}

/* Erwartete Bytefolge eines 32-Bit-Feldes nach der Wandlung zum Draht.
 * Im Swaptest-Build simuliert der Tauschpfad einen Big-Endian-Host: auf einem
 * Little-Endian-Rechner entstehen dabei absichtlich gedrehte Bytes (zwei
 * Swaptest-Builds passen zueinander, aber nicht zu einem normalen Build). */
static bool wire32_is(const char *p, unsigned int v)
{
	unsigned char le[4] = {
		(unsigned char)(v & 0xff), (unsigned char)((v >> 8) & 0xff),
		(unsigned char)((v >> 16) & 0xff), (unsigned char)((v >> 24) & 0xff) };
	unsigned char be[4] = { le[3], le[2], le[1], le[0] };

	return memcmp(p, zod_wire_swaps() ? (const char*)be : (const char*)le, 4) == 0;
}

static void fill_pattern(char *buf, int size)
{
	for(int i = 0; i < size; i++)
		buf[i] = (char)(i + 1);
}

/* Hin und zurueck muss wieder das Original ergeben. */
static void roundtrip(int pack_id, int size, const char *name)
{
	char a[4096], b[4096];

	if(size > (int)sizeof(a)) return;

	fill_pattern(a, size);
	memcpy(b, a, size);

	zod_wire_swap_payload(pack_id, b, size, 1);
	zod_wire_swap_payload(pack_id, b, size, 0);

	check(memcmp(a, b, size) == 0, std::string("Round-Trip ") + name);
}

int main()
{
	zod_wire_init();

	printf("zod_wire Test, Tauschpfad %s\n", zod_wire_swaps() ? "aktiv (Big-Endian-Simulation)" : "inaktiv (Host ist Little-Endian)");

	/* --- Primitive: feste Bytemuster, unabhaengig von der Hostreihenfolge --- */
	{
		const unsigned char le16[2] = { 0x34, 0x12 };
		const unsigned char le32[4] = { 0x78, 0x56, 0x34, 0x12 };
		unsigned char out[8];

		check(zod_rd_le16(le16) == 0x1234, "zod_rd_le16");
		check(zod_rd_le32(le32) == 0x12345678u, "zod_rd_le32");

		zod_wr_le32(out, 0x12345678u);
		check(memcmp(out, le32, 4) == 0, "zod_wr_le32");

		zod_wr_le16(out, 0x1234);
		check(memcmp(out, le16, 2) == 0, "zod_wr_le16");

		zod_wr_lef64(out, 1.0);
		/* 1.0 als IEEE-754 double, little endian */
		const unsigned char one_le[8] = { 0, 0, 0, 0, 0, 0, 0xf0, 0x3f };
		check(memcmp(out, one_le, 8) == 0, "zod_wr_lef64(1.0)");
		check(zod_rd_lef64(one_le) == 1.0, "zod_rd_lef64(1.0)");

		zod_wr_lef32(out, 2.0f);
		const unsigned char two_le[4] = { 0, 0, 0, 0x40 };
		check(memcmp(out, two_le, 4) == 0, "zod_wr_lef32(2.0)");
		check(zod_rd_lef32(two_le) == 2.0f, "zod_rd_lef32(2.0)");
	}

	/* --- Groessen der Draht-Structs: muessen dem x86-Original entsprechen --- */
	check(sizeof(object_init_packet) == 22, "sizeof(object_init_packet)==22");
	check(sizeof(object_location) == 16, "sizeof(object_location)==16");
	check(sizeof(waypoint) == 15, "sizeof(waypoint)==15");
	check(sizeof(driver_info_s) == 12, "sizeof(driver_info_s)==12");
	check(sizeof(fire_missile_info) == 16, "sizeof(fire_missile_info)==16");
	check(sizeof(destroy_object_packet) == 15, "sizeof(destroy_object_packet)==15");
	check(sizeof(set_building_state_packet) == 26, "sizeof(set_building_state_packet)==26");
	check(sizeof(bool) == 1, "sizeof(bool)==1");
	check(sizeof(int) == 4, "sizeof(int)==4");

	/* --- Feste Pakete: Round-Trip --- */
	roundtrip(ADD_NEW_OBJECT, sizeof(object_init_packet), "ADD_NEW_OBJECT");
	roundtrip(UPDATE_HEALTH, sizeof(object_health_packet), "UPDATE_HEALTH");
	roundtrip(SET_BUILDING_STATE, sizeof(set_building_state_packet), "SET_BUILDING_STATE");
	roundtrip(SET_LPLAYER_LOGINFO, sizeof(set_player_loginfo_packet), "SET_LPLAYER_LOGINFO");
	roundtrip(SEND_LOC, 4 + sizeof(object_location), "SEND_LOC");
	roundtrip(SET_SETTINGS, 1420, "SET_SETTINGS");

	/* --- Pakete mit Array: Anzahl steht im Kopf --- */
	{
		char buf[512];
		int size;

		/* SEND_WAYPOINTS: ref_id, anzahl, dann waypoints */
		size = 8 + 3 * sizeof(waypoint);
		fill_pattern(buf, size);
		int ref_id = 4711, amount = 3;
		memcpy(buf, &ref_id, 4);
		memcpy(buf + 4, &amount, 4);

		char copy[512];
		memcpy(copy, buf, size);
		zod_wire_swap_payload(SEND_WAYPOINTS, buf, size, 1);
		check(wire32_is(buf + 4, (unsigned int)amount), "SEND_WAYPOINTS Anzahl im Drahtformat");
		zod_wire_swap_payload(SEND_WAYPOINTS, buf, size, 0);
		check(memcmp(copy, buf, size) == 0, "Round-Trip SEND_WAYPOINTS");

		/* SET_OBJECT_TEAM: Anzahl steckt in einem Byte-Feld */
		size = sizeof(object_team_packet) + 2 * sizeof(driver_info_s);
		fill_pattern(buf, size);
		buf[6] = 2; /* driver_amount */
		memcpy(copy, buf, size);
		zod_wire_swap_payload(SET_OBJECT_TEAM, buf, size, 1);
		check(buf[6] == 2, "SET_OBJECT_TEAM Byte-Anzahl unveraendert");
		zod_wire_swap_payload(SET_OBJECT_TEAM, buf, size, 0);
		check(memcmp(copy, buf, size) == 0, "Round-Trip SET_OBJECT_TEAM");

		/* DESTROY_OBJECT: Anzahl der Raketen im zweiten int */
		size = sizeof(destroy_object_packet) + 2 * sizeof(fire_missile_info);
		fill_pattern(buf, size);
		int missiles = 2;
		memcpy(buf + 4, &missiles, 4);
		memcpy(copy, buf, size);
		zod_wire_swap_payload(DESTROY_OBJECT, buf, size, 1);
		zod_wire_swap_payload(DESTROY_OBJECT, buf, size, 0);
		check(memcmp(copy, buf, size) == 0, "Round-Trip DESTROY_OBJECT");
	}

	/* --- Drahtbild einzelner Felder pruefen --- */
	{
		object_health_packet p;
		char buf[sizeof(object_health_packet)];

		p.ref_id = 0x01020304;
		p.health = 0x05060708;
		memcpy(buf, &p, sizeof(p));
		zod_wire_swap_payload(UPDATE_HEALTH, buf, sizeof(p), 1);

		check(wire32_is(buf, 0x01020304u), "UPDATE_HEALTH ref_id im Drahtformat");
		check(wire32_is(buf + 4, 0x05060708u), "UPDATE_HEALTH health im Drahtformat");
	}

	/* --- Text und Rohdaten bleiben unangetastet --- */
	{
		char text[] = "hallo welt";
		char copy[sizeof(text)];

		memcpy(copy, text, sizeof(text));
		zod_wire_swap_payload(SEND_CHAT, text, sizeof(text), 1);
		check(memcmp(copy, text, sizeof(text)) == 0, "SEND_CHAT bleibt Text");

		/* STORE_MAP: nur der Kopf wird gewandelt, die Kartenbytes nicht */
		char map_pack[20];
		fill_pattern(map_pack, sizeof(map_pack));
		char map_copy[20];
		memcpy(map_copy, map_pack, sizeof(map_pack));
		zod_wire_swap_payload(STORE_MAP, map_pack, sizeof(map_pack), 1);
		check(memcmp(map_copy + 4, map_pack + 4, sizeof(map_pack) - 4) == 0, "STORE_MAP Kartenbytes unveraendert");
	}

	if(failures)
	{
		printf("[FAIL] wire_test: %d Fehler\n", failures);
		return 1;
	}

	printf("[OK] wire_test\n");
	return 0;
}
