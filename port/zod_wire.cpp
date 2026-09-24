#include "zod_wire.h"

#include <string.h>
#include <string>

#include <lib_qZod_DnSeparate/constants.h>
#include <lib_qZod_DnSeparate/event_handler.h>

using std::string;

/* ------------------------------------------------------------------ */
/* Little-Endian-Primitive                                            */
/* ------------------------------------------------------------------ */

unsigned short zod_rd_le16(const void *p)
{
	const unsigned char *b = (const unsigned char*)p;
	return (unsigned short)(b[0] | (b[1] << 8));
}

unsigned int zod_rd_le32(const void *p)
{
	const unsigned char *b = (const unsigned char*)p;
	return (unsigned int)b[0] | ((unsigned int)b[1] << 8) |
	       ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24);
}

void zod_wr_le16(void *p, unsigned short v)
{
	unsigned char *b = (unsigned char*)p;
	b[0] = (unsigned char)(v & 0xff);
	b[1] = (unsigned char)((v >> 8) & 0xff);
}

void zod_wr_le32(void *p, unsigned int v)
{
	unsigned char *b = (unsigned char*)p;
	b[0] = (unsigned char)(v & 0xff);
	b[1] = (unsigned char)((v >> 8) & 0xff);
	b[2] = (unsigned char)((v >> 16) & 0xff);
	b[3] = (unsigned char)((v >> 24) & 0xff);
}

/* Echte Byte-Reihenfolge des Hosts, zur Laufzeit bestimmt. Wichtig: nicht
 * ZOD_WIRE_SWAP verwenden -- im Swaptest-Build ist das gesetzt, obwohl der
 * Host Little-Endian bleibt. Die Primitive muessen immer echtes Little-Endian
 * liefern (Dateien, Paketkopf). */
static int host_is_le(void)
{
	const unsigned int one = 1;
	return *(const unsigned char*)&one == 1;
}

/* IEEE-754 ist auf 68k und x86 bitgleich, nur die Bytefolge unterscheidet sich. */
float zod_rd_lef32(const void *p)
{
	unsigned int v = zod_rd_le32(p);
	float f;
	memcpy(&f, &v, 4);
	return f;
}

void zod_wr_lef32(void *p, float v)
{
	unsigned int u;
	memcpy(&u, &v, 4);
	zod_wr_le32(p, u);
}

double zod_rd_lef64(const void *p)
{
	const unsigned char *b = (const unsigned char*)p;
	unsigned char tmp[8];
	double d;
	int i;

	for(i = 0; i < 8; i++)
		tmp[i] = host_is_le() ? b[i] : b[7 - i];

	memcpy(&d, tmp, 8);
	return d;
}

void zod_wr_lef64(void *p, double v)
{
	unsigned char *b = (unsigned char*)p;
	unsigned char tmp[8];
	int i;

	memcpy(tmp, &v, 8);
	for(i = 0; i < 8; i++)
		b[i] = host_is_le() ? tmp[i] : tmp[7 - i];
}

int zod_wire_swaps(void)
{
	return ZOD_WIRE_SWAP;
}

/* ------------------------------------------------------------------ */
/* Paket-Tabelle                                                       */
/* ------------------------------------------------------------------ */
/*
 * head  = Feldmuster des festen Kopfes ("4" = 32 Bit, "8" = 64 Bit,
 *         "2" = 16 Bit, "1" = Byte/bool, kein Tausch)
 * elem  = Feldmuster eines Array-Elements hinter dem Kopf (oder 0)
 * count = Index des Kopffeldes, das die Anzahl der Elemente haelt (oder -1)
 *
 * Was hinter Kopf und Array steht (Text, rohe Kartendaten), bleibt unangetastet.
 */
struct wire_desc
{
	const char *head;
	const char *elem;
	int count;
};

static wire_desc wire_table[MAX_TCP_EVENTS];
static string settings_pattern;
static bool table_ready = false;

static void set_desc(int id, const char *head, const char *elem = 0, int count = -1)
{
	if(id < 0 || id >= MAX_TCP_EVENTS) return;

	wire_table[id].head = head;
	wire_table[id].elem = elem;
	wire_table[id].count = count;
}

/* ZSettings geht als ein Block ueber die Leitung (1420 Byte):
 * 17 ZUnit_Settings (Roboter 6, Fahrzeuge 7, Geschuetze 4), dann die
 * globalen Werte. Muster wird aus der Felderfolge zusammengesetzt. */
static const char *build_settings_pattern()
{
	int i;

	if(!settings_pattern.empty()) return settings_pattern.c_str();

	for(i = 0; i < MAX_ROBOT_TYPES + MAX_VEHICLE_TYPES + MAX_CANNON_TYPES; i++)
		settings_pattern +=
			"444"  /* group_amount, move_speed, attack_radius        */
			"88"   /* attack_damage, attack_damage_chance            */
			"44"   /* attack_damage_radius, attack_missile_speed     */
			"888"  /* attack_speed, attack_snipe_chance, health      */
			"4"    /* build_time                                     */
			"8";   /* max_run_time                                   */

	settings_pattern +=
		"888888"     /* fort/robot/vehicle/repair/radar/bridge health   */
		"88888"      /* rock/grenades/rockets/hut/map item health       */
		"8"          /* grenade_damage                                  */
		"44"         /* grenade_damage_radius, grenade_missile_speed    */
		"88"         /* grenade_attack_speed, map_item_turrent_damage   */
		"44444444"   /* agro .. grenades_per_box                        */
		"8888"       /* partially/damaged/run speed, run_recharge_rate  */
		"444";       /* hut_animal_max, _min, _roam_distance            */

	return settings_pattern.c_str();
}

void zod_wire_init(void)
{
	if(table_ready) return;

	memset(wire_table, 0, sizeof(wire_table));

	/* feste Strukturen aus event_handler.h */
	set_desc(ADD_NEW_OBJECT,          "444111124");    /* object_init_packet      */
	set_desc(SET_ZONE_INFO,           "41");           /* zone_info_packet        */
	set_desc(SET_ATTACK_OBJECT,       "44");           /* attack_object_packet    */
	set_desc(UPDATE_HEALTH,           "44");           /* object_health_packet    */
	set_desc(FIRE_MISSILE,            "444");          /* fire_missile_packet     */
	set_desc(START_BUILDING,          "411");          /* start_building_packet   */
	set_desc(SET_BUILDING_STATE,      "448811");       /* set_building_state_packet */
	set_desc(PLACE_CANNON,            "4441");         /* place_cannon_packet     */
	set_desc(COMP_MSG,                "44");           /* computer_msg_packet     */
	set_desc(EJECT_VEHICLE,           "4");            /* eject_vehicle_packet    */
	set_desc(DO_CRANE_ANIM,           "441");          /* crane_anim_packet       */
	set_desc(SET_REPAIR_ANIM,         "4181");         /* repair_building_anim_packet */
	set_desc(SET_LID_OPEN,            "41");           /* set_lid_state_packet    */
	set_desc(SNIPE_OBJECT,            "4");            /* snipe_object_packet     */
	set_desc(DRIVER_HIT_EFFECT,       "4");            /* driver_hit_packet       */
	set_desc(SET_PLAYER_MODE,         "1");            /* player_mode_packet      */
	set_desc(ADD_LPLAYER,             "4");            /* add_remove_player_packet */
	set_desc(DELETE_LPLAYER,          "4");
	set_desc(SET_LPLAYER_TEAM,        "44");           /* set_player_int_packet   */
	set_desc(SET_LPLAYER_MODE,        "44");
	set_desc(SET_LPLAYER_IGNORED,     "44");
	set_desc(SET_LPLAYER_VOTEINFO,    "44");
	set_desc(SET_LPLAYER_LOGINFO,     "4444111");      /* set_player_loginfo_packet */
	set_desc(UPDATE_GAME_PAUSED,      "1");            /* update_game_paused_packet */
	set_desc(SET_GAME_PAUSED,         "1");
	set_desc(START_VOTE,              "144");          /* vote_info_packet        */
	set_desc(VOTE_INFO,               "144");
	set_desc(GIVE_PLAYER_ID,          "4");            /* player_id_packet        */
	set_desc(GIVE_LOGINOFF,           "1");            /* loginoff_packet         */
	set_desc(SET_GRENADE_AMOUNT,      "44");           /* obj_grenade_amount_packet */
	set_desc(PICKUP_GRENADE_ANIM,     "4");            /* int_packet              */
	set_desc(SET_TEAM,                "4");
	set_desc(SELECT_MAP,              "4");
	set_desc(START_BOT_EVENT,         "4");
	set_desc(STOP_BOT_EVENT,          "4");
	set_desc(RESET_MAP,               "4");
	set_desc(DO_PORTRAIT_ANIM,        "44");           /* do_portrait_anim_packet */
	set_desc(TEAM_ENDED,              "41");           /* team_ended_packet       */
	set_desc(SET_GAME_SPEED,          "4");            /* float_packet            */
	set_desc(UPDATE_GAME_SPEED,       "4");
	set_desc(GET_GAME_SPEED,          "4");
	set_desc(ADD_BUILDING_QUEUE,      "411");          /* add_building_queue_packet */
	set_desc(CANCEL_BUILDING_QUEUE,   "4411");         /* cancel_building_queue_packet */
	set_desc(DELETE_OBJECT,           "4");            /* blanker int             */
	set_desc(STOP_BUILDING,           "4");

	/* Kopf + Array */
	set_desc(SET_OBJECT_TEAM,         "4111", "48",     3);  /* + driver_info_s   */
	set_desc(DESTROY_OBJECT,          "444111", "844",  1);  /* + fire_missile_info */
	set_desc(SEND_WAYPOINTS,          "44", "144411",   1);  /* + waypoint        */
	set_desc(SEND_RALLYPOINTS,        "44", "144411",   1);
	set_desc(OBJECT_GROUP_INFO,       "444", "4",       2);  /* + minion ref_ids  */
	set_desc(SET_BUILDING_QUEUE_LIST, "44", "11",       1);  /* + ZBProductionUnit */

	/* Kopf, danach Rohdaten/Text */
	set_desc(SEND_LOC,                "44444");        /* ref_id + object_location */
	set_desc(STORE_MAP,               "4");            /* pack_num + Kartenbytes  */
	set_desc(SET_LPLAYER_NAME,        "4");            /* p_id + Name             */
	set_desc(SET_NAME,                0);              /* reiner Text             */

	/* ZSettings am Stueck */
	set_desc(SET_SETTINGS,            build_settings_pattern());

	table_ready = true;
}

static void swap_field(char *p, int n)
{
	int i;

	for(i = 0; i < n / 2; i++)
	{
		char t = p[i];
		p[i] = p[n - 1 - i];
		p[n - 1 - i] = t;
	}
}

/* Wert eines Kopffeldes in Hostreihenfolge lesen (fuer die Array-Anzahl) */
static int read_count_field(const char *p, int n)
{
	if(n == 1) return (int)(unsigned char)p[0];
	if(n == 2)
	{
		unsigned short v;
		memcpy(&v, p, 2);
		return (int)v;
	}
	if(n == 4)
	{
		int v;
		memcpy(&v, p, 4);
		return v;
	}

	return -1;
}

void zod_wire_swap_payload(int pack_id, char *data, int size, int to_wire)
{
	const wire_desc *d;
	const char *c;
	char *p = data;
	char *end = data + size;
	int field_i = 0;
	int count = -1;

	if(!zod_wire_swaps()) return;
	if(!data || size <= 0) return;
	if(pack_id < 0 || pack_id >= MAX_TCP_EVENTS) return;

	if(!table_ready) zod_wire_init();

	d = &wire_table[pack_id];
	if(!d->head) return;

	/* Kopf */
	for(c = d->head; *c; c++, field_i++)
	{
		int n = *c - '0';

		if(p + n > end) return;

		/* beim Senden steht der Zaehler noch in Hostreihenfolge,
		 * beim Empfangen erst nach dem Tausch */
		if(field_i == d->count && to_wire) count = read_count_field(p, n);
		if(n > 1) swap_field(p, n);
		if(field_i == d->count && !to_wire) count = read_count_field(p, n);

		p += n;
	}

	/* Array dahinter */
	if(!d->elem || count <= 0) return;

	while(count-- > 0)
	{
		for(c = d->elem; *c; c++)
		{
			int n = *c - '0';

			if(p + n > end) return;
			if(n > 1) swap_field(p, n);
			p += n;
		}
	}
}
