/*
 * Einzige Definition der Namens-Arrays.
 *
 * Im Original stehen sie als "const string x[] = {...}" in Headern und werden
 * dadurch in jede Uebersetzungseinheit kopiert (rund 100 Stueck): viel
 * Speicher und ebenso viele globale Konstruktoren vor main(). Die Header
 * deklarieren sie jetzt nur noch mit extern.
 */
#include <string>

using std::string;

#include <lib_qZod_DnSeparate/constants.h>
#include <lib_qZod_DnSeparate/zfont.h>
#include <lib_qZod_DnSeparate/zsdl.h>
#include <lib_qZod_DnMap/zmap_structures_old.h>
#include <lib_qZod_DnEffect/etrack.h>
#include <lib_qZod_DnObjects/zod_obj_structures.h>
#include <lib_qZod_DnObjects/zhud.h>
#include <lib_qZod_DnObjects/Animals/ahutanimal.h>
#include <lib_qZod_DnSoundEngine/zvote.h>
#include <lib_qZod_DnGui/gui_structures.h>

/* --- constants.h --- */
const string planet_type_string[MAX_PLANET_TYPES] =
{
	"desert", "volcanic", "arctic", "jungle", "city"
};

const string robot_type_string[MAX_ROBOT_TYPES] =
{
	"grunt", "psycho", "sniper", "tough", "pyro", "laser"
};

const string robot_production_string[MAX_ROBOT_TYPES] =
{
	"Grunt", "Psycho", "Sniper", "Tough", "Pyro", "Laser"
};

const string cannon_type_string[MAX_CANNON_TYPES] =
{
	"gatling", "gun", "howitzer", "missile_cannon"
};

const string cannon_production_string[MAX_CANNON_TYPES] =
{
	"Gatling", "Gun", "Howitzer", "Missile"
};

const string vehicle_type_string[MAX_VEHICLE_TYPES] =
{
	"jeep", "light", "medium", "heavy", "apc", "missile_launcher", "crane"
};

const string vehicle_production_string[MAX_VEHICLE_TYPES] =
{
	"Jeep", "Light", "Medium", "Heavy", "APC", "M Missile", "Crane"
};

const string building_type_string[MAX_BUILDING_TYPES] =
{
	"fort_front", "fort_back", "radar", "repair", "robot_factory", "vehicle_factory",
	"bridge_vert", "bridge_horz"
};

const string item_type_string[MAX_ITEM_TYPES] =
{
	"flag", "rock", "grenades", "rockets", "hut", "map_object0"
};

#ifdef ONLY_TWO_TEAMS
const string team_type_string[MAX_TEAM_TYPES] =
{
	"null", "red", "blue"
};
#else
	#ifdef USE_TEAM_COLORS
const string team_type_string[MAX_TEAM_TYPES] =
{
	"null", "red", "blue", "green", "yellow",
	"purple", "teal", "white", "black"
};
	#else
const string team_type_string[MAX_TEAM_TYPES] =
{
	"null", "red", "blue", "green", "yellow"
};
	#endif
#endif

const string player_mode_string[MAX_PLAYER_MODES] =
{
	"nobody_mode", "player_mode", "bot_mode", "spectator_mode", "tray_mode"
};

/* --- zfont.h --- */
const string font_type_string[MAX_FONT_TYPES] =
{
	"big_white", "small_white", "green_building", "loading_white",
	"yellow_menu"
};

/* --- zsdl.h --- */
const string sound_setting_string[MAX_SOUND_SETTINGS] =
{
	"0%", "25%", "50%", "75%", "100%"
};

/* --- zmap_structures_old.h --- */
const string map_object_type_string[MAX_MAP_OBJECT_TYPES] =
{
	"rock", "bridge", "building", "cannon", "vehicle", "robot", "animal", "map_item"
};

/* --- etrack.h --- */
const string etrack_type_string[MAX_ETRACK_TYPES] =
{
	"tank", "jeep"
};

/* --- zod_obj_structures.h --- */
const string portrait_anim_string[MAX_PORTRAIT_ANIMS] =
{
	"YES_SIR_ANIM", "YES_SIR3_ANIM", "UNIT_REPORTING1_ANIM",
	"UNIT_REPORTING2_ANIM", "GRUNTS_REPORTING_ANIM",
	"PSYCHOS_REPORTING_ANIM", "SNIPERS_REPORTING_ANIM",
	"TOUGHS_REPORTING_ANIM", "LASERS_REPORTING_ANIM",
	"PYROS_REPORTING_ANIM", "WERE_ON_OUR_WAY_ANIM",
	"HERE_WE_GO_ANIM", "YOUVE_GOT_IT_ANIM", "MOVING_IN_ANIM",
	"OKAY_ANIM", "ALRIGHT_ANIM", "NO_PROBLEM_ANIM", "OVER_N_OUT_ANIM",
	"AFFIRMATIVE_ANIM", "GOING_IN_ANIM", "LETS_DO_IT_ANIM",
	"LETS_GET_EM_ANIM", "WERE_UNDER_ATTACK_ANIM",
	"I_SAID_WERE_UNDER_ATTACK_ANIM", "HELP_HELP_ANIM",
	"THEYRE_ALL_OVER_US_ANIM", "WERE_LOSEING_IT_ANIM",
	"AAAHHH_ANIM", "FOR_CHRIST_SAKE_ANIM",
	"YOURE_JOKING_ANIM", "TARGET_DESTROYED_ANIM", "BLINK_ANIM",
	"WINK_ANIM", "SURPRISE_ANIM", "ANGER_ANIM", "GRIN_ANIM",
	"SCARED_ANIM", "EYES_LEFT_ANIM", "EYES_RIGHT_ANIM",
	"EYES_UP_ANIM", "EYES_DOWN_ANIM", "WHISTLE_ANIM",
	"LOOK_LEFT_ANIM", "LOOK_RIGHT_ANIM", "SALUTE_ANIM",
	"THUMBS_UP_ANIM", "YES_SIR_SALUTE_ANIM", "GOING_IN_THUMBS_UP_ANIM",
	"FORGET_IT_ANIM", "GET_OUTTA_HERE_ANIM", "GOOD_HIT_ANIM",
	"NO_WAY_ANIM", "NICE_ONE_ANIM", "OH_YEAH_ANIM", "GOTCHA_ANIM",
	"SMOOKIN_ANIM", "COOL_ANIM", "WIPE_OUT_ANIM", "TERRITORY_TAKEN_ANIM",
	"FIRE_EXTINGUISHED_ANIM", "GUN_CAPTURED_ANIM", "VEHICLE_CAPTURED_ANIM",
	"GRENADES_COLLECTED_ANIM", "ENDW1_ANIM", "ENDW2_ANIM", "ENDW3_ANIM",
	"ENDL1_ANIM", "ENDL2_ANIM", "ENDL3_ANIM",
};

/* --- zhud.h --- */
const string hub_buttons_string[MAX_HUD_BUTTONS] =
{
	"a_button", "b_button", "d_button", "g_button", "menu_button", "r_button", "t_button", "v_button", "z_button"
};

/* --- ahutanimal.h --- */
const string hut_animal_type_string[MAX_HUT_ANIMAL_TYPES] =
{
	"green_snake", "green_lizard", "desert_rabit", "raptor",
	"mini_raptor", "pig_dino", "yellow_worm", "arctic_rabit",
	"penguin", "white_wolf", "ostrich", "rat", "turtle", "red_worm",
	"green_eyed_fox"
};

/* --- zvote.h --- */
const string vote_type_string[MAX_VOTE_TYPES] =
{
	"Pause Game", "Resume Game", "Change Map", "Start Bot", "Stop Bot",
	"Reset Game", "Reshuffle Teams", "Set Game Speed"
};

/* --- gui_structures.h --- */
const string gmm_event_type_string[MAC_GMM_EVENTS] =
{
	"unknown", "click", "unclick", "motion", "keypress",
	"wheelup", "wheeldown"
};

const string mmwidget_type_string[MAX_MMWIDGETS] =
{
	"unknown", "button", "label", "list", "radio", "team_color"
};

const string mmbutton_type_string[MAX_MMBUTTON_TYPES] =
{
	"", "close"
};

const string gmm_main_menu_button_string[MAX_GMMMM_BUTTONS] =
{
	"Change Teams", "Manage Bots", "Player List",
	"Select Map", "Multiplayer", "Options", "Quit Game"
};
