/*
 * Eigener RTG-Schirm und eigene Eingabe (Ersatz fuer SDL 1.2 auf AmigaOS).
 *
 * Aufbau eines Bildes:
 *   1. Die Engine zeichnet ausschliesslich in eine Flaeche im Fast-RAM
 *      ("compose"), die SDL_SetVideoMode zurueckgibt. Dort ist jeder Zugriff
 *      schnell und gecacht.
 *   2. SDL_Flip kopiert sie in einem Durchlauf in den Bildschirmspeicher.
 *      Das ist ein linearer Durchgang ueber die Busgrenze -- bei 8 Bit
 *      307200 Byte, bei 16 Bit das Doppelte.
 *
 * Bewusst KEINE doppelte Pufferung: Auf der V2 ist ein echter Pufferwechsel
 * gemessen 136 us schnell, aber alles, was nicht in JEDEM Bild neu gezeichnet
 * wird, flackert dann. Mit dem Zusammensetzen im RAM gibt es
 * das Problem nicht.
 *
 * Systemfreundlich: eigener CUSTOMSCREEN ueber cybergraphics.library, kein
 * Hardware-Banging, kein Forbid, kein WaitTOF (das kostet auf AmigaOS bis zu
 * 20 ms und hat frueher schon die Bildrate gedeckelt).
 */
#include <SDL/SDL.h>

#include <string.h>
#include <stdlib.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/modeid.h>
#include <exec/io.h>
#include <devices/inputevent.h>
#include <devices/input.h>   /* IND_WRITEEVENT */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/keymap.h>
#include <proto/dos.h>   /* GetVar -- getenv sieht SetEnv unter libnix nicht */

#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>

#include "zod_log.h"
#include "zod_palette.h"
#include "amiga_startup.h"
#include "ammx.h"
#include "fineclock.h"

/* port/amiga/sdl_video.cpp -- Schmutzspuren je Zeile */
extern "C" void zod_dirty_set_target(SDL_Surface *s);
extern "C" int  zod_dirty_out_runs(int blockzeile, short *x0, short *x1, int max);
extern "C" int  zod_dirty_block_shift(void);
extern "C" int  zod_dirty_get_luecke(void);
extern "C" int  zod_dirty_out_zeile(int blockzeile);

static int breit_an = 0;

/* ---- Wohin `umschalten` wirklich geht -----------------------------------
 *
 * Auf der V1200 meldete ein Lauf 2407 us fuer 148 429 Byte in 673 Aufrufen.
 * Die Kopiermessung derselben Maschine (ZOD_COPYBENCH: 350 ns je Aufruf,
 * 6,02 ns je Byte) sagt dafuer 1128 us -- es fehlen also rund 1200 us je Bild,
 * mehr als die Kopie selbst. Kandidaten: das Warten auf `LockBitMapTagList`,
 * das Auswerten des Blockrasters, und kalte Quelldaten (die Messung kopiert
 * denselben Puffer wieder und wieder, das Spiel frisch Gezeichnetes).
 *
 * Erklaeren waere Raten. Deshalb drei getrennte Zaehler. */
static unsigned long flip_sperre = 0;
static unsigned long flip_kopie  = 0;
static unsigned long flip_rest   = 0;
static unsigned long flip_bilder = 0;

/* ---- Bildschirmfoto AUS DEM GRAFIKSPEICHER ------------------------------
 *
 * WARUM ES DAS BRAUCHT: `ZOD_SHOT` speichert `screen->pixels` -- das ist die
 * QUELLE der Kopie, nicht ihr Ziel. Jedes so erzeugte Bild belegt, dass die
 * Engine richtig zeichnet, und ist blind fuer alles, was beim Kopieren in den
 * Schirm schiefgeht: Schmutzspur, Blockraster, Laufzusammenlegung, AMMX.
 *
 * Genau dort liegt der ganze Code der letzten Tage. Ein schwarzer Schirm waere
 * auf jedem `ZOD_SHOT`-Bild gruen geblieben -- die Pruefungen auf Blocknaehte
 * haben also nie geprueft, was sie pruefen sollten.
 *
 * `SetEnv ZOD_SHOTOUT <datei>:<bildnummer>` liest nach dem Kopieren aus dem
 * Grafikspeicher zurueck. Der Vergleich mit dem `ZOD_SHOT`-Bild derselben
 * Bildnummer ist der eigentliche Nachweis: beide muessen gleich sein. */
static unsigned char *shotout_puffer = 0;
static char shotout_name[128];
static unsigned long shotout_bild = 0;
static unsigned long shotout_bpr = 0;
static int shotout_faellig = 0;
extern "C" void zod_dirty_clear(void);
extern "C" void zod_dirty_sweep(void);
extern unsigned long long zod_dirty_bytes;
extern unsigned long zod_dirty_frames;
extern unsigned long zod_dirty_calls;
#include "fastcopy.h"

/* Die Basis selbst halten: libstubs.a traegt fuer cybergraphics den falschen
 * Bibliotheksnamen ("CyberGfx.library" statt "cybergraphics.library"). */
struct Library *CyberGfxBase = 0;

extern "C" {

void zod_sdl_set_shared_palette(const SDL_Color *colors);
const SDL_Color *zod_sdl_shared_palette(void);
void zod_sdl_build_16bit_table(int r_bits, int g_bits, int b_bits, int swapped);

/* ------------------------------------------------------------- Zustand */

static struct Screen *screen_ptr = 0;
static struct Window *window_ptr = 0;
static UWORD *blank_pointer = 0;
static struct Library *KeymapBaseOwn = 0;

static SDL_Surface *compose = 0;
static SDL_VideoInfo video_info;
static SDL_PixelFormat video_format;

static int screen_bpp = 0;        /* Tiefe des RTG-Schirms */
static int screen_swapped = 0;    /* 16 Bit byte-vertauscht (RGB16PC) */

static int mouse_x = 0, mouse_y = 0;
/* Korrekturversatz fuer SDL_WarpMouse -- Einzelheiten dort. */
static int warp_dx = 0, warp_dy = 0;

/* Ein noch nicht angekommener Warp. Siehe SDL_WarpMouse. */
static int warp_warte  = 0;
static int warp_ziel_x = 0, warp_ziel_y = 0;
static int warp_roh_x  = 0, warp_roh_y  = 0;
static int unicode_on = 0;
static int wheel_pending = 0;     /* +1 hoch, -1 runter */

/* ---- Den ECHTEN Zeiger bewegen ------------------------------------------
 *
 * SDL_WarpMouse muss den Systemzeiger versetzen, nicht nur die gemeldete
 * Lage. Der vorgesehene Weg auf AmigaOS ist ein IECLASS_POINTERPOS-Ereignis
 * an input.device -- kein Eingriff in Intuition-Innereien, sondern die
 * dokumentierte Schnittstelle.
 *
 * Warum es nicht ohne geht: Die Engine warpt beim Start einmal in die
 * Bildschirmmitte (ZPlayer::InitSDL). Wuerde nur ein Korrekturversatz
 * gefuehrt, laege die gemeldete Lage danach dauerhaft um diesen Versatz
 * neben der echten -- steht der Systemzeiger beim Start am linken Rand,
 * waere die linke Haelfte des Schirms nicht mehr erreichbar. Das waere
 * schlechter als vorher. */
static struct MsgPort  *input_port = 0;
static struct IOStdReq *input_req  = 0;
static int input_offen = 0;

static void input_oeffnen(void)
{
	if(input_offen) return;

	input_port = CreateMsgPort();
	if(!input_port) return;

	input_req = (struct IOStdReq*)CreateIORequest(input_port, sizeof(struct IOStdReq));
	if(!input_req)
	{
		DeleteMsgPort(input_port);
		input_port = 0;
		return;
	}

	if(OpenDevice((CONST_STRPTR)"input.device", 0,
	              (struct IORequest*)input_req, 0) == 0)
	{
		input_offen = 1;
		return;
	}

	DeleteIORequest((struct IORequest*)input_req);
	DeleteMsgPort(input_port);
	input_req = 0;
	input_port = 0;
}

static void input_schliessen(void)
{
	if(input_offen)
	{
		CloseDevice((struct IORequest*)input_req);
		input_offen = 0;
	}

	if(input_req)  { DeleteIORequest((struct IORequest*)input_req); input_req = 0; }
	if(input_port) { DeleteMsgPort(input_port); input_port = 0; }
}

static int zeiger_setzen(int x, int y)
{
	struct InputEvent ie;

	if(!input_offen) return 0;

	memset(&ie, 0, sizeof(ie));
	ie.ie_Class     = IECLASS_POINTERPOS;
	ie.ie_Code      = IECODE_NOBUTTON;
	ie.ie_Qualifier = 0;            /* absolute Schirmlage */
	ie.ie_X         = (WORD)x;
	ie.ie_Y         = (WORD)y;

	input_req->io_Command = IND_WRITEEVENT;
	input_req->io_Flags   = 0;
	input_req->io_Length  = sizeof(ie);
	input_req->io_Data    = (APTR)&ie;

	return DoIO((struct IORequest*)input_req) == 0;
}

static void close_all(void)
{
	if(window_ptr)
	{
		ClearPointer(window_ptr);
		CloseWindow(window_ptr);
		window_ptr = 0;
	}

	if(blank_pointer)
	{
		FreeMem(blank_pointer, 6 * sizeof(UWORD));
		blank_pointer = 0;
	}

	if(screen_ptr)
	{
		CloseScreen(screen_ptr);
		screen_ptr = 0;
	}

	if(KeymapBaseOwn)
	{
		CloseLibrary(KeymapBaseOwn);
		KeymapBaseOwn = 0;
	}

	if(CyberGfxBase)
	{
		CloseLibrary(CyberGfxBase);
		CyberGfxBase = 0;
	}

	input_schliessen();
}

void SDL_Quit(void)
{
	if(compose)
	{
		SDL_FreeSurface(compose);
		compose = 0;
	}

	close_all();
}

/* --------------------------------------------------------- Schirm oeffnen */

static ULONG find_mode(int w, int h, int depth)
{
	struct TagItem tags[] = {
		{ CYBRBIDTG_NominalWidth,  (ULONG)w },
		{ CYBRBIDTG_NominalHeight, (ULONG)h },
		{ CYBRBIDTG_Depth,         (ULONG)depth },
		{ TAG_DONE, 0 }
	};

	ULONG id = BestCModeIDTagList(tags);

	if(id == INVALID_ID || !IsCyberModeID(id)) return INVALID_ID;

	return id;
}

static void load_palette_now(void)
{
	const SDL_Color *colors = zod_sdl_shared_palette();

	if(screen_bpp != 8 || !screen_ptr) return;

	/* LoadRGB32: Kopfwort (Anzahl<<16 | erstes Register), dann je Farbe drei
	 * 32-Bit-Komponenten, links ausgerichtet. 0x01010101 ist die richtige
	 * Streckung 8 -> 32 Bit (0xFF wird zu 0xFFFFFFFF). */
	static ULONG table[1 + 256 * 3 + 1];

	table[0] = (256UL << 16) | 0;

	for(int i = 0; i < 256; i++)
	{
		table[1 + i * 3 + 0] = (ULONG)colors[i].r * 0x01010101UL;
		table[1 + i * 3 + 1] = (ULONG)colors[i].g * 0x01010101UL;
		table[1 + i * 3 + 2] = (ULONG)colors[i].b * 0x01010101UL;
	}

	table[1 + 256 * 3] = 0;

	LoadRGB32(&screen_ptr->ViewPort, table);
}

void zod_sdl_palette_changed(void)
{
	load_palette_now();

	/* Die Tabelle wird auch bei einem 8-Bit-Schirm gebraucht: SDL_rotozoom
	 * legt intern Echtfarbflaechen an, und der Blitter 8 -> 16 greift darauf
	 * zu. Ohne Tabelle kehrt er still zurueck, ohne zu zeichnen. Bei einem
	 * 8-Bit-Schirm gilt die Hostreihenfolge -- vertauscht wird nur, was
	 * wirklich in den Schirm geht. */
	zod_sdl_build_16bit_table(5, 6, 5, screen_bpp == 16 ? screen_swapped : 0);
}

/* Die Bank eines Planeten hat sich geaendert (ZMap::RenderMap).
 *
 * Im 8-Bit-Pfad tragen die Bilder keine eigene Palette -- die Farben stehen
 * einmal im Schirm. Ein Kartenwechsel auf einen anderen Planeten muss sie
 * deshalb hier nachziehen, sonst zeigt der Boden die Farben des vorigen. */
extern "C" void zod_sdl_reload_palette(void)
{
	if(!zod_palette_ready()) return;

	zod_sdl_set_shared_palette(zod_palette_colors());
	zod_sdl_palette_changed();
}

/* Weiter unten in dieser Datei -- sie liegt im extern "C"-Block, die
 * Vorausdeklaration muss deshalb auch darin stehen. */
static void copy_bench(SDL_Surface *flaeche);

void zod_flip_report(void)
{
	const unsigned long f = zod_fineclock_freq();

	if(!f || !flip_bilder) return;

	/* Ticks -> us, ohne Ueberlauf. */
	const unsigned long t = f / 1000UL;

	ZLOG("Umschalten je Bild: sperren %ld us, kopieren %ld us, rest %ld us\n",
	     (long)((flip_sperre / flip_bilder) * 1000UL / t),
	     (long)((flip_kopie  / flip_bilder) * 1000UL / t),
	     (long)((flip_rest   / flip_bilder) * 1000UL / t));
}

SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags)
{
	/* ES GIBT HIER KEINEN FENSTERMODUS, und das ist eine Entscheidung,
	 * keine Luecke (23.09., nach Ruecksprache mit dem Nutzer).
	 *
	 * Dieser Unterbau oeffnet einen EIGENEN RTG-Schirm. Daran haengt der
	 * ganze 8-Bit-Weg: die gemeinsame Palette wird per LoadRGB32 auf den
	 * eigenen ViewPort gesetzt, und SDL_Flip kopiert Indizes blockweise
	 * in die Schirm-BitMap. Auf einem Fenster der Workbench gibt es
	 * beides nicht -- dort muesste jedes Bild Punkt fuer Punkt in deren
	 * Format umgesetzt werden. Gemessen kostete genau das 868 statt
	 * 192 us im Emulator; auf der V1200 waeren es grob 4 bis 8 ms je
	 * Bild bei 12 ms Bildzeit.
	 *
	 * Dazu kaemen Fensterversatz und Zuschnitt in SDL_Flip,
	 * IDCMP_REFRESHWINDOW samt vollem Neuaufbau (heute deckt
	 * BACKDROP|BORDERLESS|NOCAREREFRESH das ab, weil uns niemand
	 * verdecken kann) und fensterrelative Mauskoordinaten.
	 *
	 * Das Ankreuzfeld im Launcher ist deshalb entfernt. `-w` bleibt fuer
	 * den Host-Bau gueltig; hier wird es GEMELDET statt still verworfen
	 * -- ein stiller Schalter ist von einem kaputten nicht zu
	 * unterscheiden. */
	if(!(flags & SDL_FULLSCREEN))
		ZLOG("Schirm: -w (Fenster) gibt es auf dem Amiga nicht -- Vollbild\n");

	/* AMMX einmal pruefen, bevor der erste Blit laeuft. Schreibt selbst ins
	 * Protokoll, welcher Weg gilt. */
	zod_ammx_init();
	zod_copy_init();

	/* Palette aus dem Archivpfad uebernehmen, falls vorhanden. */
	if(zod_palette_ready())
		zod_sdl_set_shared_palette(zod_palette_colors());

	if(screen_ptr)
	{
		/* Groessenwechsel: alles neu aufbauen */
		if(compose) { SDL_FreeSurface(compose); compose = 0; }
		close_all();
	}

	if(!CyberGfxBase)
	{
		CyberGfxBase = OpenLibrary((CONST_STRPTR)CYBERGFXNAME, 41L);

		if(!CyberGfxBase)
		{
			ZLOG("Schirm: cybergraphics.library nicht zu oeffnen -- RTG noetig\n");
			return 0;
		}
	}

	if(!KeymapBaseOwn)
		KeymapBaseOwn = OpenLibrary((CONST_STRPTR)"keymap.library", 36L);

	/* Fuer SDL_WarpMouse; scheitert es, bleibt der Korrekturversatz. */
	input_oeffnen();

	/* 8 Bit ist das Ziel; wenn der Treiber keinen LUT8-Modus anbietet, 16 Bit. */
	int want[3];
	int n = 0;

	if(bpp == 8) { want[n++] = 8; want[n++] = 16; }
	else { want[n++] = bpp ? bpp : 16; want[n++] = 8; }

	ULONG id = INVALID_ID;
	int got = 0;

	for(int i = 0; i < n && id == INVALID_ID; i++)
	{
		id = find_mode(w, h, want[i]);

		if(id != INVALID_ID) got = want[i];
	}

	if(id == INVALID_ID)
	{
		ZLOG("Schirm: kein RTG-Modus fuer %dx%d gefunden\n", w, h);
		return 0;
	}

	ULONG err = 0;
	struct TagItem sa[] = {
		{ SA_DisplayID,  id },
		{ SA_Width,      (ULONG)w },
		{ SA_Height,     (ULONG)h },
		{ SA_Depth,      (ULONG)got },
		{ SA_Type,       CUSTOMSCREEN },
		{ SA_Quiet,      TRUE },
		{ SA_ShowTitle,  FALSE },
		{ SA_Draggable,  FALSE },
		{ SA_AutoScroll, FALSE },
		{ SA_SharePens,  TRUE },
		{ SA_ErrorCode,  (ULONG)&err },
		{ TAG_DONE, 0 }
	};

	screen_ptr = OpenScreenTagList(0, sa);

	if(!screen_ptr)
	{
		ZLOG("Schirm: OpenScreen fehlgeschlagen (Fehler %ld)\n", (long)err);
		return 0;
	}

	screen_bpp = got;

	ULONG pixfmt = GetCyberMapAttr(screen_ptr->RastPort.BitMap, CYBRMATTR_PIXFMT);

	screen_swapped = (pixfmt == PIXFMT_RGB16PC || pixfmt == PIXFMT_RGB15PC);

	struct TagItem wa[] = {
		{ WA_CustomScreen, (ULONG)screen_ptr },
		{ WA_Left, 0 }, { WA_Top, 0 },
		{ WA_Width, (ULONG)w }, { WA_Height, (ULONG)h },
		{ WA_Flags, WFLG_BACKDROP | WFLG_BORDERLESS | WFLG_ACTIVATE |
		            WFLG_RMBTRAP | WFLG_REPORTMOUSE | WFLG_NOCAREREFRESH },
		{ WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY |
		            IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW },
		{ TAG_DONE, 0 }
	};

	window_ptr = OpenWindowTagList(0, wa);

	if(!window_ptr)
	{
		ZLOG("Schirm: OpenWindow fehlgeschlagen\n");
		close_all();
		return 0;
	}

	/* Der Systemzeiger muss weg -- die Engine zeichnet ihren eigenen. */
	blank_pointer = (UWORD*)AllocMem(6 * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);

	if(blank_pointer) SetPointer(window_ptr, blank_pointer, 1, 16, 0, 0);

	/* Die Flaeche, in die die Engine zeichnet: immer 8 Bit, wenn eine
	 * gemeinsame Palette vorliegt. */
	const int compose_bpp = (screen_bpp == 8 || zod_palette_ready()) ? 8 : 16;

	compose = SDL_CreateRGBSurface(0, w, h, compose_bpp, 0, 0, 0, 0);

	if(!compose)
	{
		ZLOG("Schirm: Zeichenflaeche %dx%d nicht zu belegen\n", w, h);
		close_all();
		return 0;
	}

	compose->flags |= SDL_FULLSCREEN | (screen_bpp == 8 ? SDL_HWPALETTE : 0);

	/* Ab jetzt wird mitgeschrieben, welche Zeilenbereiche beschrieben werden.
	 * Das erste Bild gilt als vollstaendig schmutzig -- was im Schirm steht,
	 * wissen wir nicht. */
	zod_dirty_set_target(compose);

	/* Die Schalter der Ausgabe -- HIER, nicht in SDL_Flip. Siehe die
	 * Begruendung ueber copy_bench: innerhalb von LockBitMap darf nichts
	 * Blockierendes laufen, und GetVar liest ENV: von der Platte. */
	{
		char wahl[16];

		if(GetVar((CONST_STRPTR)"ZOD_BREIT", (STRPTR)wahl, sizeof(wahl), 0) > 0)
			if(wahl[0] == 'e' || wahl[0] == 'E' || wahl[0] == '1')
				breit_an = 1;

		ZLOG("Ausgabe: %s\n",
		     breit_an ? "ganze Blockzeilen (ZOD_BREIT=ein)"
		              : "Laeufe je Blockzeile (Vorgabe)");

		if(GetVar((CONST_STRPTR)"ZOD_COPYBENCH", (STRPTR)wahl, sizeof(wahl), 0) > 0
		   && (wahl[0] == '1' || wahl[0] == 'j' || wahl[0] == 'J'))
			copy_bench(compose);

		char wunsch[160];

		if(GetVar((CONST_STRPTR)"ZOD_SHOTOUT", (STRPTR)wunsch, sizeof(wunsch), 0) > 0)
		{
			/* Geteilt wird am LETZTEN Doppelpunkt, und nur wenn dahinter
			 * ausschliesslich Ziffern stehen.
			 *
			 * WARUM: Auf AmigaOS ist der Doppelpunkt der VOLUME-Trenner.
			 * Eine erste Fassung teilte am ersten -- aus `ZOD:s1.bmp:600`
			 * wurde der Dateiname `ZOD`, und weil AmigaDOS
			 * schreibungsunabhaengig ist, hat das BMP im Arbeitsverzeichnis
			 * `ZOD:game` die PROGRAMMDATEI `zod` ueberschrieben. `ZOD_SHOT`
			 * macht es seit je richtig (`strrchr`, zplayer.cpp) -- hier war
			 * es nachgebaut und dabei verschlimmert. */
			int trenn = -1;

			for(int i = (int)strlen(wunsch) - 1; i >= 0; i--)
				if(wunsch[i] == ':') { trenn = i; break; }

			if(trenn >= 0 && wunsch[trenn + 1])
			{
				int nur_ziffern = 1;

				for(int i = trenn + 1; wunsch[i]; i++)
					if(wunsch[i] < '0' || wunsch[i] > '9') { nur_ziffern = 0; break; }

				if(nur_ziffern)
				{
					for(int i = trenn + 1; wunsch[i]; i++)
						shotout_bild = shotout_bild * 10
						             + (unsigned long)(wunsch[i] - '0');

					wunsch[trenn] = 0;
				}
			}

			strncpy(shotout_name, wunsch, sizeof(shotout_name) - 1);
			shotout_name[sizeof(shotout_name) - 1] = 0;

			if(!shotout_bild) shotout_bild = 1;

			/* Zwei Sicherungen, nachdem dieses Werkzeug einmal die
			 * Programmdatei ueberschrieben hat: ohne Namen wird nichts
			 * geschrieben, und der Name MUSS auf .bmp enden. Damit kann ein
			 * Tippfehler im Pfad keine Programm-, Archiv- oder Kartendatei
			 * mehr treffen -- eine Messhilfe darf nichts zerstoeren koennen. */
			const int nl = (int)strlen(shotout_name);
			const int heisst_bmp = nl > 4
			                    && (shotout_name[nl-4] == '.')
			                    && (shotout_name[nl-3] == 'b' || shotout_name[nl-3] == 'B')
			                    && (shotout_name[nl-2] == 'm' || shotout_name[nl-2] == 'M')
			                    && (shotout_name[nl-1] == 'p' || shotout_name[nl-1] == 'P');

			/* KEIN `return` hier -- dahinter stehen `zod_sdl_palette_changed`
			 * und die Videoinfo. Ein Abbruch an dieser Stelle waere
			 * schlimmer als der Tippfehler, den er meldet. */
			if(!heisst_bmp)
			{
				ZLOG("Foto aus dem Grafikspeicher: '%s' endet nicht auf .bmp,"
				     " entfaellt\n", shotout_name);

				shotout_name[0] = 0;
			}
			else
			{
				shotout_puffer = (unsigned char*)malloc((size_t)compose->h
				                                        * (size_t)compose->pitch);

				ZLOG("Foto aus dem Grafikspeicher: '%s' bei Bild %ld%s\n",
				     shotout_name, (long)shotout_bild,
				     shotout_puffer ? "" : " -- KEIN SPEICHER, entfaellt");
			}
		}
	}

	zod_sdl_palette_changed();

	memset(&video_info, 0, sizeof(video_info));
	video_info.current_w = w;
	video_info.current_h = h;
	video_info.hw_available = 1;
	video_info.blit_fill = 1;
	video_format = *compose->format;
	video_info.vfmt = &video_format;

	ZLOG("Schirm: %dx%d, %d Bit (Zeichenflaeche %d Bit)%s\n",
	     w, h, screen_bpp, compose_bpp, screen_swapped ? ", byte-vertauscht" : "");


	return compose;
}

SDL_Surface *SDL_GetVideoSurface(void)
{
	return compose;
}

const SDL_VideoInfo *SDL_GetVideoInfo(void)
{
	return &video_info;
}

/* --------------------------------------------------------------- Umschalten */

/* ---- Kopier-Messung: Zeit gegen Stueckgroesse, bei GLEICHER Byte-Zahl ------
 *
 * Anlass ist ein Widerspruch in den Messungen des Nutzers auf der V1200:
 *
 *     480 Aufrufe zu   640 Byte -> 307 200 Byte in 2966 us  (104 MB/s)
 *      30 Aufrufe zu 10240 Byte -> 307 200 Byte in 5282 us  ( 58 MB/s)
 *
 * Gleiche Arbeit, weniger Aufrufe, MEHR Zeit. Kein Modell aus Aufrufpreis und
 * Bytepreis kann das erzeugen -- beide Preise sind positiv. Entweder ist die
 * grosse Einzelkopie in den Grafikspeicher wirklich langsamer (Schreibpuffer),
 * oder die beiden Laeufe waren nicht vergleichbar: im zweiten lief der
 * Skalierer mit doppelt so viel Arbeit. Aus Spielmessungen ist das nicht zu
 * trennen -- jede Spielsituation ist anders. Diese Messung braucht keine.
 *
 * ZWEI REGELN, die hier teuer gelernt wurden:
 *
 * 1. **Innerhalb von LockBitMap wird NICHT protokolliert und NICHTS von der
 *    Platte gelesen.** Die Sperre haelt den Grafiktreiber an; `GetVar` liest
 *    ENV: von der Platte, `ZLOG` schreibt auf den seriellen Kanal, und beides
 *    blockiert. Eine erste Fassung las die Schalter und gab die Tabelle
 *    innerhalb der Sperre aus -- im Emulator lief das durch, auf der V1200
 *    blieb der SCHIRM SCHWARZ. Deshalb: sperren, messen, entsperren, dann
 *    ausgeben.
 * 2. Die E-Clock laeuft mit rund 709 kHz, ein Tick ist 1,41 us. Ein einzelner
 *    Durchgang von 307 200 Byte dauert auf der V1200 rund 3000 us (2100
 *    Ticks), unter FS-UAE aber 1 bis 2 -- dort misst man dann die UHR, nicht
 *    das Kopieren ("307 200 Byte in 1 us" = 300 GB/s). Die Zahl der
 *    Durchgaenge wird deshalb verdoppelt, bis die Spanne 2000 Ticks
 *    ueberschreitet: Quantisierungsfehler unter 0,05 %, auf jeder Maschine,
 *    ohne dass die Messung wissen muss, wie schnell sie ist.
 *
 * `SetEnv ZOD_COPYBENCH 1`. Sie schreibt einmal Unsinn in den Schirm -- das
 * naechste Bild ueberschreibt ihn. */
static void copy_bench(SDL_Surface *flaeche)
{
	static const long stueck[] = { 16, 32, 64, 128, 256, 640, 1280, 2560,
	                               10240, 0 };
	enum { MIND_TICKS = 2000, RUNDEN = 3, ANZAHL = 10 };

	const unsigned long f = zod_fineclock_freq();

	if(!f)         { ZLOG("Kopier-Messung: keine timer.device\n"); return; }
	if(!screen_ptr) return;

	APTR base = 0;
	ULONG bpr = 0;
	struct TagItem lbmi[] = {
		{ LBMI_BASEADDRESS, (ULONG)&base },
		{ LBMI_BYTESPERROW, (ULONG)&bpr },
		{ TAG_DONE, 0 }
	};

	APTR lock = LockBitMapTagList(screen_ptr->RastPort.BitMap, lbmi);

	if(!lock) { ZLOG("Kopier-Messung: Schirm nicht zu sperren\n"); return; }

	if((int)bpr != flaeche->pitch)
	{
		const long b = (long)bpr, p = (long)flaeche->pitch;

		UnLockBitMap(lock);
		ZLOG("Kopier-Messung: Zeilenabstand ungleich (%ld gegen %ld),"
		     " uebersprungen\n", b, p);

		return;
	}

	unsigned char *dst = (unsigned char*)base;
	const unsigned char *src = (const unsigned char*)flaeche->pixels;
	const unsigned long gesamt = (unsigned long)flaeche->h * (unsigned long)bpr;

	unsigned long t_us[ANZAHL], t_dg[ANZAHL];

	/* --- ab hier bis zum UnLockBitMap keine Ausgabe, kein GetVar --- */
	for(int i = 0; i < ANZAHL; i++)
	{
		const unsigned long n = stueck[i] ? (unsigned long)stueck[i] : gesamt;
		unsigned long durchgaenge = 1;
		unsigned long best = 0;

		for(int runde = 0; runde < RUNDEN; runde++)
		{
			unsigned long d;

			for(;;)
			{
				const unsigned long t0 = zod_fineclock_ticks();

				for(unsigned long p = 0; p < durchgaenge; p++)
					for(unsigned long o = 0; o < gesamt; o += n)
					{
						unsigned long m = gesamt - o;

						if(m > n) m = n;

						zod_copy_row(dst + o, src + o, (int)m);
					}

				d = zod_fineclock_ticks() - t0;

				if(d >= MIND_TICKS || durchgaenge >= 4096) break;

				durchgaenge *= 2;
			}

			/* Bester Wert, nicht Mittel -- der schlechteste enthaelt
			 * Unterbrechungen des Systems. */
			if(!best || d < best) best = d;
		}

		/* Nicht (best*1000000)/f -- das laeuft ueber. */
		t_us[i] = (best * 1000UL) / (f / 1000UL) / durchgaenge;
		t_dg[i] = durchgaenge;
	}

	UnLockBitMap(lock);

	ZLOG("Kopier-Messung: je Durchgang %ld Byte, bester von %ld Runden\n",
	     (long)gesamt, (long)RUNDEN);

	for(int i = 0; i < ANZAHL; i++)
	{
		const unsigned long n = stueck[i] ? (unsigned long)stueck[i] : gesamt;

		ZLOG("  %6ld Byte je Aufruf: %5ld Aufrufe, %5ld us, %4ld KB/ms"
		     " (%ld Durchgaenge)\n",
		     (long)n, (long)((gesamt + n - 1) / n), (long)t_us[i],
		     (long)(t_us[i] ? gesamt / t_us[i] : 0), (long)t_dg[i]);
	}
}

int SDL_Flip(SDL_Surface *screen)
{
	if(!screen || !screen_ptr) return -1;

	APTR base = 0;
	ULONG bpr = 0;
	struct TagItem lbmi[] = {
		{ LBMI_BASEADDRESS, (ULONG)&base },
		{ LBMI_BYTESPERROW, (ULONG)&bpr },
		{ TAG_DONE, 0 }
	};

	const unsigned long t_vor = zod_fineclock_ticks();

	APTR lock = LockBitMapTagList(screen_ptr->RastPort.BitMap, lbmi);

	if(!lock) return -1;

	const unsigned long t_gesperrt = zod_fineclock_ticks();

	unsigned char *dst = (unsigned char*)base;
	const unsigned char *src = (const unsigned char*)screen->pixels;

	const int h = screen->h;
	const int w = screen->w;

	if(screen_bpp == 8 && screen->format->BytesPerPixel == 1)
	{
		/* Nur die vermerkten Bereiche kopieren. Die Anzeige (100 Punkte
		 * rechts, 36 unten = 67 440 von 307 200 Byte) aendert sich meist gar
		 * nicht und wurde bisher trotzdem in jedem Bild mitgeschoben.
		 *
		 * Kein memcpy: siehe fastcopy.h -- ein Bibliotheksaufruf je Zeile,
		 * und CopyMem kopiert hoechstens 48 Byte je movem-Paar. */
		unsigned long gezaehlt = 0;
		const int schub = zod_dirty_block_shift();
		const int block = 1 << schub;

		/* DIE KOSTENSTRUKTUR, aus drei Messungen auf der V1200 (19.09.):
		 *
		 *   ein Byte                              8,73 ns  (114 MB/s im Spiel)
		 *   ein Aufruf an verstreuter Adresse      1140 ns
		 *   ein zusammenhaengender Aufruf           ~0     (bei einem je Bild)
		 *
		 * Ein verstreuter Aufruf ist damit 131 Byte wert = 8 Bloecke. Das ist
		 * der Umschlagpunkt, an dem `ZOD_LUECKE` haengt.
		 *
		 * Die drei Messpunkte, aus denen das stammt:
		 *   963 verstreute Aufrufe, 109 656 Byte -> 2057 us
		 *     1 Aufruf,             277 363 Byte -> 2424 us
		 *   ZOD_COPYBENCH, 307 200 Byte am Stueck -> 1851 us (bester von 3)
		 * Aus den letzten beiden folgt der Faktor 1,45 zwischen Bestwert der
		 * Messung und Mittel im Spiel -- der ist KEINE Kopierstrategie, sondern
		 * Grundlast und Streuung.
		 *
		 * Bewertet ergibt das: Laeufe mit Luecke 8 rund 1981 us, ganze
		 * Blockzeilen 2423, immer ganzes Bild 2683. Deshalb Laeufe.
		 * `SetEnv ZOD_BREIT ein` schaltet auf ganze Blockzeilen um -- gebaut,
		 * gemessen, verworfen, aber als Gegenprobe behalten.
		 *
		 * Kein memcpy: siehe fastcopy.h -- ein Bibliotheksaufruf je Zeile,
		 * und CopyMem kopiert hoechstens 48 Byte je movem-Paar. */
		const int gleicher_abstand = ((int)bpr == screen->pitch);

		if(breit_an)
		{
			/* Ganze schmutzige Blockzeilen, senkrecht zusammengefasst. */
			const int zeilen_gesamt = (h + block - 1) >> schub;
			int bz = 0;

			while(bz < zeilen_gesamt)
			{
				if(!zod_dirty_out_zeile(bz)) { bz++; continue; }

				int bis = bz;

				while(bis + 1 < zeilen_gesamt && zod_dirty_out_zeile(bis + 1))
					bis++;

				const int y0 = bz << schub;
				int zeilen = h - y0;
				const int hoechstens = (bis - bz + 1) << schub;

				if(zeilen > hoechstens) zeilen = hoechstens;

				unsigned char *d = dst + (unsigned long)y0 * bpr;
				const unsigned char *s = src + (unsigned long)y0
				                       * (unsigned long)screen->pitch;

				if(gleicher_abstand)
				{
					const unsigned long m = (unsigned long)zeilen
					                      * (unsigned long)bpr;

					zod_copy_row(d, s, (int)m);
					gezaehlt += m;
					zod_dirty_calls++;
				}
				else
				{
					for(int r = 0; r < zeilen; r++)
					{
						zod_copy_row(d, s, w);
						gezaehlt += (unsigned long)w;
						zod_dirty_calls++;

						d += bpr;
						s += screen->pitch;
					}
				}

				bz = bis + 1;
			}
		}
		else
		{
			/* Laeufe je Blockzeile -- der gemessen guenstigste Weg. Ein Lauf,
			 * der die Breite bis auf die Luecke deckt, wird aufgefuellt; deckt
			 * er sie ganz und ist der Zeilenabstand gleich, liegen die 16
			 * Zeilen zusammenhaengend und es genuegt EIN Aufruf. */
			const int auffuellen = zod_dirty_get_luecke() * block;

			enum { MAX_LAEUFE = 24 };
			short lx0[MAX_LAEUFE], lx1[MAX_LAEUFE];

			for(int y0 = 0; y0 < h; y0 += block)
			{
				int zeilen = h - y0;

				if(zeilen > block) zeilen = block;

				const int n_laeufe = zod_dirty_out_runs(y0 >> schub, lx0, lx1,
				                                        MAX_LAEUFE);

				for(int i = 0; i < n_laeufe; i++)
				{
					short x0 = lx0[i], x1 = lx1[i];

					if(x0 < 0) x0 = 0;
					if(x1 > w - 1) x1 = (short)(w - 1);

					if(x1 < x0) continue;

					int n = x1 - x0 + 1;

					if(gleicher_abstand && (w - n) <= auffuellen)
					{
						x0 = 0;
						n = w;
					}

					unsigned char *d = dst + (unsigned long)y0 * bpr + x0;
					const unsigned char *s = src + (unsigned long)y0
					                       * (unsigned long)screen->pitch + x0;

					if(gleicher_abstand && n == w)
					{
						const unsigned long m = (unsigned long)zeilen
						                      * (unsigned long)bpr;

						zod_copy_row(d, s, (int)m);
						gezaehlt += m;
						zod_dirty_calls++;

						continue;
					}

					for(int r = 0; r < zeilen; r++)
					{
						zod_copy_row(d, s, n);
						gezaehlt += (unsigned long)n;
						zod_dirty_calls++;

						d += bpr;
						s += screen->pitch;
					}
				}
			}
		}

		zod_dirty_bytes += gezaehlt;
		zod_dirty_frames++;
		zod_dirty_sweep();
		zod_dirty_clear();

		/* Ruecklesen, solange gesperrt ist -- gespeichert wird erst danach.
		 * Innerhalb der Sperre wird nicht protokolliert und nichts auf die
		 * Platte geschrieben (siehe copy_bench). */
		if(shotout_puffer && zod_dirty_frames == shotout_bild)
		{
			memcpy(shotout_puffer, dst, (size_t)h * (size_t)bpr);
			shotout_bpr = bpr;
			shotout_faellig = 1;
		}
	}
	else if(screen->format->BytesPerPixel == 1)
	{
		/* 8 Bit zeichnen, 16 Bit ausgeben: Zeile fuer Zeile umsetzen. */
		extern const Uint16 *zod_sdl_table16(void);
		const Uint16 *tab = zod_sdl_table16();

		if(!tab) { UnLockBitMap(lock); return -1; }

		for(int y = 0; y < h; y++)
		{
			Uint16 *d = (Uint16*)dst;

			for(int x = 0; x < w; x++) d[x] = tab[src[x]];

			dst += bpr;
			src += screen->pitch;
		}
	}
	else
	{
		for(int y = 0; y < h; y++)
		{
			memcpy(dst, src, (size_t)w * screen->format->BytesPerPixel);
			dst += bpr;
			src += screen->pitch;
		}
	}

	const unsigned long t_kopiert = zod_fineclock_ticks();

	UnLockBitMap(lock);

	flip_sperre += t_gesperrt - t_vor;
	flip_kopie  += t_kopiert - t_gesperrt;
	flip_rest   += zod_fineclock_ticks() - t_kopiert;
	flip_bilder++;

	if(shotout_faellig)
	{
		shotout_faellig = 0;

		SDL_Surface *bild = SDL_CreateRGBSurfaceFrom(shotout_puffer, w, h, 8,
		                                            (int)shotout_bpr,
		                                            0, 0, 0, 0);

		if(bild)
		{
			if(bild->format->palette && screen->format->palette)
				memcpy(bild->format->palette->colors,
				       screen->format->palette->colors,
				       sizeof(SDL_Color) * 256);

			ZLOG("Foto aus dem Grafikspeicher: '%s' bei Bild %ld%s\n",
			     shotout_name, (long)shotout_bild,
			     SDL_SaveBMP(bild, shotout_name) == 0 ? " geschrieben"
			                                          : " FEHLGESCHLAGEN");

			SDL_FreeSurface(bild);
		}

		free(shotout_puffer);
		shotout_puffer = 0;
	}

	return 0;
}

void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
	(void)x; (void)y; (void)w; (void)h;
	SDL_Flip(screen);
}

/* ------------------------------------------------------------- Eingabe */

/* RAWKEY -> SDL-Tastennummer. Die Engine prueft die Zahlen teilweise roh. */
static int rawkey_to_sym(UWORD scan)
{
	switch(scan)
	{
	case 0x45: return SDLK_ESCAPE;
	case 0x44: return SDLK_RETURN;
	case 0x41: return SDLK_BACKSPACE;
	case 0x42: return SDLK_TAB;
	case 0x40: return SDLK_SPACE;
	case 0x46: return SDLK_DELETE;
	case 0x4C: return SDLK_UP;
	case 0x4D: return SDLK_DOWN;
	case 0x4E: return SDLK_RIGHT;
	case 0x4F: return SDLK_LEFT;
	case 0x50: return SDLK_F1;
	case 0x51: return SDLK_F2;
	case 0x52: return SDLK_F3;
	case 0x53: return SDLK_F4;
	case 0x54: return SDLK_F5;
	case 0x55: return SDLK_F6;
	case 0x56: return SDLK_F7;
	case 0x57: return SDLK_F8;
	case 0x58: return SDLK_F9;
	case 0x59: return SDLK_F10;
	case 0x60: return SDLK_LSHIFT;
	case 0x61: return SDLK_RSHIFT;
	case 0x63: return SDLK_LCTRL;
	case 0x64: return SDLK_LALT;
	case 0x65: return SDLK_RALT;
	default: break;
	}

	/* Zifferntasten der Hauptreihe: 1..0 liegen auf 0x01..0x0A */
	if(scan >= 0x01 && scan <= 0x09) return SDLK_1 + (scan - 0x01);
	if(scan == 0x0A) return SDLK_0;

	return SDLK_UNKNOWN;
}

static Uint32 qual_to_mod(UWORD qual)
{
	Uint32 mod = 0;

	if(qual & IEQUALIFIER_LSHIFT) mod |= KMOD_LSHIFT;
	if(qual & IEQUALIFIER_RSHIFT) mod |= KMOD_RSHIFT;
	if(qual & IEQUALIFIER_CONTROL) mod |= KMOD_LCTRL;
	if(qual & IEQUALIFIER_LALT) mod |= KMOD_LALT;
	if(qual & IEQUALIFIER_RALT) mod |= KMOD_RALT;

	return mod;
}

static Uint16 rawkey_to_char(UWORD code, UWORD qual)
{
	if(!KeymapBaseOwn) return 0;

	struct InputEvent ie;
	UBYTE buf[8];

	memset(&ie, 0, sizeof(ie));
	ie.ie_Class = IECLASS_RAWKEY;
	ie.ie_Code = code;
	ie.ie_Qualifier = qual;

	LONG n = MapRawKey(&ie, (STRPTR)buf, sizeof(buf), 0);

	return (n > 0) ? buf[0] : 0;
}

/* Systemzeiger zeigen oder verstecken.
 *
 * Das Spiel setzt beim Start einen LEEREN Zeiger (blank_pointer) -- es
 * zeichnet seinen eigenen. Auf den Vollbildschirmen (Laden, Sieg,
 * Niederlage) zeichnet es aber nichts, und dann ist gar kein Zeiger da:
 * Der Nutzer meldete "Im Lose Bildschirm sehe ich keinen Mauszeiger und
 * kann keine der drei Optionen auswaehlen".
 *
 * ClearPointer gibt den Standardpfeil zurueck, SetPointer nimmt ihn
 * wieder weg. */
extern "C" void zod_schirm_zeiger_amiga(int an)
{
	if(!window_ptr) return;

	if(an) ClearPointer(window_ptr);
	else if(blank_pointer) SetPointer(window_ptr, blank_pointer, 1, 16, 0, 0);
}

/* Nach einer Videosequenz: Spielschirm zurueck nach vorn, Eingabe leeren.
 *
 * Der externe Player oeffnet seinen eigenen Vollbildschirm. Ist er fertig,
 * schliesst er ihn -- aber welcher Schirm dann vorn liegt und welches
 * Fenster aktiv ist, entscheidet Intuition, nicht wir.
 *
 * Wichtiger noch ist das Leeren: ESC beendet den Film, und dieselbe Taste
 * oeffnet im Spiel die Rueckfrage "wirklich beenden?". Ein liegengebliebener
 * Tastendruck wuerde also nach JEDEM abgebrochenen Film diese Frage
 * aufmachen -- ein Fehler, den man dem Film nicht ansieht. Verworfen wird
 * ALLES, was im Port steht, nicht nur ESC: waehrend der Film lief, hat kein
 * Klick und keine Taste dem Spiel gegolten. */
extern "C" void zod_schirm_zurueck(void)
{
	if(!window_ptr) return;

	if(screen_ptr) ScreenToFront(screen_ptr);

	ActivateWindow(window_ptr);

	{
		struct IntuiMessage *im;
		int n = 0;

		while((im = (struct IntuiMessage *)GetMsg(window_ptr->UserPort)))
		{
			ReplyMsg((struct Message *)im);
			n++;
		}

		if(n) ZLOG("Video: %ld liegengebliebene Ereignisse verworfen\n", (long)n);
	}
}

int SDL_PollEvent(SDL_Event *event)
{
	if(!window_ptr) return 0;

	/* Mausrad aus dem letzten Durchlauf nachreichen */
	if(wheel_pending && event)
	{
		memset(event, 0, sizeof(*event));
		event->type = SDL_MOUSEBUTTONDOWN;
		event->button.type = SDL_MOUSEBUTTONDOWN;
		event->button.button = (Uint8)(wheel_pending > 0 ? SDL_BUTTON_WHEELUP
		                                                 : SDL_BUTTON_WHEELDOWN);
		event->button.state = SDL_PRESSED;
		event->button.x = (Uint16)mouse_x;
		event->button.y = (Uint16)mouse_y;
		wheel_pending = 0;
		return 1;
	}

	/* Mausbewegung: einmal je Aufruf aus dem Schirm lesen statt je Pixel eine
	 * Nachricht zu erzeugen. */
	if(screen_ptr && event)
	{
		const int rx = screen_ptr->MouseX;
		const int ry = screen_ptr->MouseY;
		int nx, ny;

		/* Ist ein Warp unterwegs, ist erst hier zu sehen, ob er angekommen
		 * ist -- IND_WRITEEVENT kehrt zurueck, sobald das Ereignis in der
		 * Kette liegt, nicht wenn Intuition den Zeiger versetzt hat. */
		if(warp_warte)
		{
			if(rx == warp_ziel_x && ry == warp_ziel_y)
			{
				/* Angekommen. Ab jetzt ist die Rohlage die Wahrheit. */
				warp_dx = 0;
				warp_dy = 0;
				warp_warte = 0;
			}
			else if(rx != warp_roh_x || ry != warp_roh_y)
			{
				/* Die Rohlage hat sich geaendert, ist aber nicht das Ziel:
				 * der Warp ist nicht angekommen (input.device fehlt) oder
				 * der Nutzer hat dazwischen selbst bewegt. Dann gilt, was
				 * da ist -- mit dem Versatz, der es aufs Ziel abbildet. */
				warp_dx = warp_ziel_x - rx;
				warp_dy = warp_ziel_y - ry;
				warp_warte = 0;
			}
		}

		if(warp_warte)
		{
			/* Noch unterwegs: das ZIEL melden, nicht die alte Rohlage plus
			 * Versatz. Das ist der ganze Punkt -- siehe SDL_WarpMouse. */
			nx = warp_ziel_x;
			ny = warp_ziel_y;
		}
		else
		{
			nx = rx + warp_dx;
			ny = ry + warp_dy;
		}

		/* Der Versatz kann die gemeldete Lage aus dem Schirm schieben. Ein
		 * Wert ausserhalb waere kein Schoenheitsfehler: die Engine liest
		 * `mouse_x > screen->w - 10` als "am rechten Rand" und begaenne zu
		 * rollen, und ihr eigener Zeiger landete neben dem Bild. */
		if(nx < 0) nx = 0;
		if(ny < 0) ny = 0;
		if(nx > screen_ptr->Width  - 1) nx = screen_ptr->Width  - 1;
		if(ny > screen_ptr->Height - 1) ny = screen_ptr->Height - 1;

		if(nx != mouse_x || ny != mouse_y)
		{
			memset(event, 0, sizeof(*event));
			event->type = SDL_MOUSEMOTION;
			event->motion.type = SDL_MOUSEMOTION;
			event->motion.xrel = (Sint16)(nx - mouse_x);
			event->motion.yrel = (Sint16)(ny - mouse_y);
			mouse_x = nx;
			mouse_y = ny;
			event->motion.x = (Uint16)mouse_x;
			event->motion.y = (Uint16)mouse_y;
			return 1;
		}
	}

	struct IntuiMessage *im;

	while((im = (struct IntuiMessage*)GetMsg(window_ptr->UserPort)))
	{
		const ULONG cls = im->Class;
		const UWORD code = im->Code;
		const UWORD qual = im->Qualifier;

		ReplyMsg((struct Message*)im);

		if(!event) continue;

		memset(event, 0, sizeof(*event));

		if(cls == IDCMP_MOUSEBUTTONS)
		{
			int down = 0, button = 0;

			switch(code)
			{
			case SELECTDOWN: down = 1; button = SDL_BUTTON_LEFT; break;
			case SELECTUP:   down = 0; button = SDL_BUTTON_LEFT; break;
			case MENUDOWN:   down = 1; button = SDL_BUTTON_RIGHT; break;
			case MENUUP:     down = 0; button = SDL_BUTTON_RIGHT; break;
			case MIDDLEDOWN: down = 1; button = SDL_BUTTON_MIDDLE; break;
			case MIDDLEUP:   down = 0; button = SDL_BUTTON_MIDDLE; break;
			default: continue;
			}

			event->type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
			event->button.type = event->type;
			event->button.button = (Uint8)button;
			event->button.state = (Uint8)(down ? SDL_PRESSED : SDL_RELEASED);
			event->button.x = (Uint16)mouse_x;
			event->button.y = (Uint16)mouse_y;

			return 1;
		}

		if(cls == IDCMP_RAWKEY)
		{
			const int up = (code & IECODE_UP_PREFIX) != 0;
			const UWORD scan = code & 0x7F;

			/* Mausrad nach der NewMouse-Uebereinkunft */
			if(scan == 0x7A || scan == 0x7B)
			{
				if(up) continue;

				wheel_pending = (scan == 0x7A) ? +1 : -1;
				continue;
			}

			event->type = up ? SDL_KEYUP : SDL_KEYDOWN;
			event->key.type = event->type;
			event->key.state = (Uint8)(up ? SDL_RELEASED : SDL_PRESSED);
			event->key.keysym.scancode = (Uint8)scan;
			event->key.keysym.sym = (Uint32)rawkey_to_sym(scan);
			event->key.keysym.mod = qual_to_mod(qual);

			if(unicode_on && !up)
				event->key.keysym.unicode = rawkey_to_char(code, qual);

			/* Tasten ohne eigene Nummer ueber ihr Zeichen melden */
			if(event->key.keysym.sym == SDLK_UNKNOWN)
			{
				Uint16 ch = rawkey_to_char(code, qual);

				if(ch >= 32 && ch < 127) event->key.keysym.sym = ch;
			}

			return 1;
		}
	}

	return 0;
}

void SDL_PumpEvents(void)
{
	SDL_Event dummy;

	while(SDL_PollEvent(&dummy)) { }
}

Uint8 SDL_EventState(Uint8 type, int state)
{
	(void)type; (void)state;
	return SDL_ENABLE;
}

int SDL_EnableUNICODE(int enable)
{
	const int old = unicode_on;

	if(enable >= 0) unicode_on = enable;

	return old;
}

int SDL_EnableKeyRepeat(int delay, int interval)
{
	(void)delay; (void)interval;
	return 0;
}

int SDL_ShowCursor(int toggle)
{
	(void)toggle;
	return 0;          /* der Systemzeiger ist immer aus */
}

/* Den echten Zeiger bewegt AmigaOS nur ueber input.device; das ist ein
 * Eingriff ins System, den dieser Port nicht macht. Stattdessen ein
 * KORREKTURVERSATZ: gemeldet wird `Schirmlage + warp_d*`, und ein Warp setzt
 * den Versatz so, dass die gemeldete Lage genau am Ziel liegt.
 *
 * Warum das noetig ist -- der Fehler, den es behebt: Die alte Fassung setzte
 * nur `mouse_x/mouse_y` und liess den Versatz weg. Beim Ziehen mit der
 * MITTLEREN Maustaste (`mclick`-Weg in zplayer_events.cpp) warpt die Engine
 * den Zeiger nach jedem Bild auf ihren Ankerpunkt zurueck. Ohne Versatz
 * meldete der naechste Abruf wieder die echte, davon abweichende Lage --
 * also in JEDEM Bild dieselbe Bewegung, die sofort wieder zurueckgesetzt
 * wurde. Auf dem Schirm stand der Zeiger fest, waehrend die Karte
 * davonlief.
 *
 * Grenze, die bleibt: Der PHYSISCHE Zeiger wandert weiter und kann den
 * Schirmrand erreichen. Dort liefert er keine Bewegung mehr, das Ziehen
 * endet also; einmal loslassen und neu ansetzen loest es. Ein echtes
 * Zurueckbewegen bekaeme man nur ueber input.device. */
void SDL_WarpMouse(Uint16 x, Uint16 y)
{
	static int gemeldet = 0;

	int ok = zeiger_setzen((int)x, (int)y);
	int px = 0, py = 0;

	if(screen_ptr)
	{
		px = screen_ptr->MouseX;
		py = screen_ptr->MouseY;
	}

	mouse_x = x;
	mouse_y = y;

	if(px == (int)x && py == (int)y)
	{
		/* Schon da -- kein Versatz noetig. */
		warp_dx = 0;
		warp_dy = 0;
		warp_warte = 0;
	}
	else
	{
		/* NOCH NICHT DA, und daraus jetzt einen Versatz zu rechnen war der
		 * Fehler.
		 *
		 * IND_WRITEEVENT kehrt zurueck, sobald das Ereignis in der
		 * Eingabekette liegt -- NICHT, wenn Intuition den Zeiger versetzt
		 * hat. Wer unmittelbar danach MouseX liest, bekommt oft noch die
		 * ALTE Lage. Der daraus gerechnete Versatz ist dann dauerhaft
		 * falsch, und zwar um den ganzen Weg:
		 *
		 *   Zeiger stand rechts unten (1000,700), Ziel Mitte (320,240)
		 *   -> Versatz -680,-460
		 *   -> ein Bild spaeter sitzt der Zeiger wirklich auf 320,240
		 *   -> gemeldet wird 320-680 = -360, geklemmt auf 0
		 *   -> die Engine liest "Zeiger am linken oberen Rand" und ROLLT.
		 *
		 * Genau das hat der Nutzer gemeldet: "Die Maus wird meistens oben
		 * links in der Ecke positioniert direkt bei Spielstart. Dadurch
		 * scrollt die Karte dann automatisch nach oben links." Und
		 * "meistens" ist die Signatur eines Wettlaufs -- stand der Zeiger
		 * zufaellig schon in der Mitte, war der Versatz null und alles
		 * richtig.
		 *
		 * Statt zu raten wird jetzt GEWARTET: bis die Rohlage das Ziel
		 * zeigt, meldet SDL_PollEvent das Ziel. Kein Delay -- das Warten
		 * kostet keine Zeit, es wird nur nichts Falsches behauptet. */
		warp_ziel_x = (int)x;
		warp_ziel_y = (int)y;
		warp_roh_x  = px;
		warp_roh_y  = py;
		warp_warte  = 1;

		/* Rueckfall, falls input.device gar nicht geht: dann bleibt es beim
		 * Versatz wie bisher (siehe SDL_PollEvent). */
		warp_dx = (int)x - px;
		warp_dy = (int)y - py;
	}

	/* Einmal messen statt annehmen. `Versatz 0,0` heisst: der Zeiger sass
	 * schon beim Zurueckkehren von DoIO, es gab gar keinen Wettlauf. */
	if(!gemeldet)
	{
		gemeldet = 1;
		ZLOG("Zeiger setzen: %s, verlangt %ld,%ld erreicht %ld,%ld, %s\n",
		     input_offen ? (ok ? "input.device" : "input.device MELDET FEHLER")
		                 : "nicht moeglich (nur Korrekturversatz)",
		     (long)x, (long)y, (long)px, (long)py,
		     warp_warte ? "noch unterwegs -- es wird gewartet" : "Versatz 0,0");
	}
}

/* Der Zeiger ist auf einem eigenen RTG-Schirm ohnehin eingesperrt -- ein
 * echtes "Greifen" gibt es auf AmigaOS also gar nicht zu tun. Der Zustand
 * MUSS aber gefuehrt werden, denn die Engine benutzt ihn als SCHALTER, nicht
 * als Frage an das Fenstersystem:
 *
 *   ZPlayer::StartMouseScrolling  -- kehrt bei GRAB_OFF sofort zurueck
 *   DoMouseScroll{Left,Right,Up,Down} -- alle vier verlangen GRAB_ON
 *   Taste 'm' -- schaltet um ("mouse taken" / "mouse released")
 *
 * Die alte Attrappe gab immer GRAB_OFF zurueck. Damit war das Rollen der
 * Karte am Bildrand seit dem ersten Bau des Ports tot, und zwar lautlos:
 * Pfeiltasten und Minikarte gingen weiter, es sah also nach einer fehlenden
 * Funktion aus statt nach einem Fehler.
 *
 * Vorgabe GRAB_OFF, weil ZPlayer::InitSDL selbst GRAB_ON setzt -- so wird
 * genau der Weg benutzt, den auch der PC-Bau nimmt. */
static int zod_grab_mode = SDL_GRAB_OFF;

int SDL_WM_GrabInput(int mode)
{
	if(mode == SDL_GRAB_ON || mode == SDL_GRAB_OFF)
		zod_grab_mode = mode;

	return zod_grab_mode;
}

Uint8 SDL_GetMouseState(int *x, int *y)
{
	if(x) *x = mouse_x;
	if(y) *y = mouse_y;

	return 0;
}

void SDL_WM_SetCaption(const char *title, const char *icon)
{
	(void)title; (void)icon;
}

void SDL_WM_SetIcon(SDL_Surface *icon, Uint8 *mask)
{
	(void)icon; (void)mask;
}

} /* extern "C" */
