# ZodEngine-Port — ein Makefile fuer Host (Linux) und AmigaOS.
#
#   make                               Host-Build: build/host/zod
#   make PLATFORM=amiga CPU=68040 hello  Amiga-Rauchtest (im Toolchain-Container, siehe tools/build.sh)
#
# CPU: 68040 | 68060 | 68080. Bewusst -m680x0 statt generischem -m68881,
# damit gcc keine FPSP-trappenden Opcodes (fsin/fatan) fuer 040/060 erzeugt.

PLATFORM ?= host
CPU      ?= 68040

include sources.mk

DEFINES  := -DDISABLE_MYSQL -DDISABLE_OPENGL -DDISABLE_REGCHECK -DDISABLE_BUYREG
# Paket-Log fuer Protokoll-Goldens; zur Laufzeit nur aktiv mit Umgebungsvariable ZOD_PACKET_LOG=<datei>
DEFINES  += -DZOD_PACKET_LOG
INCLUDES := -Iinclude -Icompat/qt -Iport -Iport/amiga

ifeq ($(PLATFORM),host)
  BUILD    := build/host
  CXX      := g++
  CC       := gcc
  CXXFLAGS := -std=gnu++11 -O2 -g -ffast-math -Wno-write-strings -Wno-deprecated-declarations
  CFLAGS   := -O2 -g
  LIBS     := $(shell sdl-config --libs 2>/dev/null) -lSDL_image -lSDL_ttf -lSDL_mixer -lpthread -lm
  SDLFLAGS := $(shell sdl-config --cflags 2>/dev/null)
  ENGINE   := $(BUILD)/zod
else ifeq ($(PLATFORM),shim)
  # Die GANZE Engine auf dem Host, aber gegen die EIGENE Grafikschicht
  # (port/amiga/sdl_video.cpp) statt gegen SDL.
  #
  # Warum es das gibt: Fehler dieser Schicht zeigen sich erst im Zusammenspiel
  # mit der Engine -- eine kleine Sonde findet sie nicht, und ein Emulatorlauf
  # kostet Minuten und liefert nur Zahlen. Hier laeuft dasselbe in Sekunden,
  # mit AddressSanitizer, und ZOD_SHOT liefert ein Bild.
  #
  # Nur der Schirm ist ersetzt (tests/shim/shim_screen.cpp): nichts wird
  # angezeigt, gezeichnet wird trotzdem -- und genau das laesst sich ansehen.
  BUILD    := build/shim
  CXX      := g++
  CC       := gcc
  CXXFLAGS := -std=gnu++11 -O1 -g -ffast-math -Wno-write-strings -Wno-deprecated-declarations
  CFLAGS   := -O1 -g
  ifeq ($(ASAN),1)
    CXXFLAGS += -fsanitize=address
    CFLAGS   += -fsanitize=address
    LDFLAGS  += -fsanitize=address
    BUILD    := build/shim-asan
  endif
  LIBS     := -lpthread -lm
  SDLFLAGS :=
  # eigene Header VOR allen Systempfaden
  # NUR die SDL-Koepfe aus port/amiga/include -- der Rest dort (net, netdb.h,
  # sys, arpa) sind Amiga-Ersatzkoepfe fuer BSD-Sockets und wuerden hier die
  # echten Linux-Koepfe verdecken. tests/shim/include/SDL ist ein Verweis.
  INCLUDES := -Itests/shim/include -Iinclude -Icompat/qt -Iport -Iport/amiga \
              -Iport/third_party
  ENGINE_SRCS += port/amiga/sdl_addons.cpp port/amiga/sdl_video.cpp \
                 port/amiga/sdl_misc.cpp tests/shim/shim_screen.cpp
  ENGINE   := $(BUILD)/zod
else ifeq ($(PLATFORM),amiga)
  CXX      := m68k-amigaos-g++
  CC       := m68k-amigaos-gcc
  # 68080: bewusst reines -m68060. gcc -m68080 erzeugt mov3q/moviw.l (LineA, nur
  # gueltig mit Apollo-Bit im SR) -> Guru 8000000A auf der V1200; -mtune=68080
  # erzeugt addiw.l/cmpiw.l. 080-Spezifika spaeter gezielt in ASM-Modulen (AMMX).
  # 68080 wird bewusst mit -m68060 uebersetzt, NICHT mit -m68040.
  #
  # Belegt durch Opcode-Zensus (16.09.): gcc darf fuer -m68040 kein fintrz
  # benutzen und ersetzt jede double->int-Wandlung durch eine Umschaltung des
  # Rundungsmodus -- 7 Befehle statt 2, und fmovem.l ...,fpcr serialisiert die
  # FPU-Pipeline. Im Binary: 815 fpcr-Referenzen gegen 29 (rund 272 Wandlungen).
  # Auf dem 68080 ist fintrz Hardware, der 040-Zuschnitt verschenkt das also
  # genau im Zeichenpfad. Die reinen Pixelschleifen sind bei beiden wortgleich.
  #
  # -m68080 bleibt verboten: erzeugt mov3q/moviw.l (LineA) -> Guru 8000000A.
  ifeq ($(CPU),68080)
    CPUFLAGS := -m68060
    # AMMX (Apollo 68080) nur hier. Der Assembler der Werkzeugkette kennt die
    # Befehle NICHT (GNU as 2.39: load/store/paddb/vperm unbekannt) -- vasm
    # kennt sie und liegt im Container. Ein 040/060-Bau enthaelt damit keinen
    # einzigen AMMX-Opcode, was staerker ist als eine Laufzeitpruefung.
    ZOD_AMMX := 1
  else
    CPUFLAGS := -m$(CPU)
  endif
  # Diese Toolchain erzeugt ohne -mhard-float Soft-Float (___muldf3), auch bei -m68060.
  # Multilibs werden nur ueber -mcpu=68020 [-m68881] ausgewaehlt; mit -m68040/-m68060
  # linkt der Treiber sonst die 68000-Soft-Float-Libs (double-Rueckgabe in d0/d1
  # statt fp0 -> ABI-Bruch). Deshalb: Code fuer die Ziel-CPU, Link gegen libm020[/libm881].
  ifeq ($(FPU),soft)
    CPUFLAGS += -msoft-float
    LINKLIB  := -m68020
    BUILD    := build/amiga-soft
  else
    CPUFLAGS += -mhard-float
    LINKLIB  := -m68020 -m68881
    BUILD    := build/amiga
  endif

  # EIN Ausgabeverzeichnis fuer alle drei CPUs -- die Ergebnisse heissen
  # zod_040, zod_060, zod_080 und liegen nebeneinander, so wie sie auch ins
  # Paket kommen.
  #
  # Die OBJEKTE bleiben dagegen strikt je CPU getrennt. Das ist keine
  # Ordnungsfrage: make kennt keine Abhaengigkeit von den Uebersetzerflaggen
  # und wuerde ein .o aus einem -m68040-Lauf kommentarlos in einen
  # -m68060-Bau binden. Genau so ist hier schon einmal ein Mischbau
  # entstanden.
  CPUSUF := $(patsubst 68%,%,$(CPU))
  # Bebbo-Optimierung "e" (tote Zuweisungen/ueberfluessige Ladebefehle) ist aus:
  # sie haelt "fintrzx fp0,fp0" fuer einen ueberfluessigen Ladebefehl und
  # loescht ihn. Jede Wandlung (int)(berechneter double) rundet dann per
  # fmove.l zur NAECHSTEN Ganzzahl statt abzuschneiden (belegt 17.09. am
  # Pass-Dump: fintrz bis bbro vorhanden, nach "bebbo's-optimizers" weg).
  # Betrifft -m68060/68080; -m68040 wandelt ueber die fpcr-Umschaltung.
  # Folge im Spiel: Lebensbalken 36*0,1081 -> gruen 4, gelb 3 -> Breite -1
  # -> als Uint16 65535 -> Balken bis zum Schirmrand. fabs/fneg/fsqrt auf
  # demselben Register bleiben erhalten (geprueft). Default waere abcefhilmnprsz0.
  BBBFLAGS := -fbbb=abcfhilmnprsz0
  # -fomit-frame-pointer ist bei -O2 auf m68k NICHT von selbst an. Gemessen an
  # derselben Quelle, nur die Flagge unterschiedlich:
  #
  #     link.w a5 in zobject.cpp        223  ->  23
  #     Codegroesse ueber sechs heisse Dateien  -2,35 %
  #
  # Jedes link/unlk-Paar sind vier Speicherzugriffe je Aufruf, und a5 ist
  # dadurch belegt -- m68k hat nur a0..a6, und a6 traegt fast immer eine
  # Bibliotheksbasis. Risiko praktisch null, weil -fno-exceptions gesetzt ist.
  #
  # Im DIAG-Baum bleibt sie WEG: ohne Rahmenzeiger wird der Stapelruecklauf
  # unter MuForce/MuGuardianAngel schlechter, und genau dafuer gibt es den
  # Baum. Siehe den DIAG-Block weiter unten.
  OMITFP   := -fomit-frame-pointer
  CXXFLAGS := -noixemul $(CPUFLAGS) $(BBBFLAGS) -O2 -ffast-math -fno-exceptions -fno-rtti -fno-threadsafe-statics
  CFLAGS   := -noixemul $(CPUFLAGS) $(BBBFLAGS) -O2
  # verzoegert ausgewertet (=), AMIGA_SDL_LIB wird weiter unten gesetzt
  LDFLAGS   = -noixemul $(LINKLIB) -L$(AMIGA_SDL_LIB)
  # -lamiga: Stubs fuer die AmigaOS-Bibliotheken und Geraete (u. a. AHI in P5)
  LIBS     := -lamiga -ldebug -lm
  # SDL 1.2 liegt im Toolchain-Image ausserhalb der Standardsuche
  AMIGA_SDL_INC ?= /opt/m68k-amigaos/usr/include
  AMIGA_SDL_LIB ?= /opt/m68k-amigaos/usr/lib
  # Ersatz-Header (SDL_image/ttf/mixer, BSD-Sockets) vor den Systempfaden
  INCLUDES += -Iplatform/amiga -Iport/amiga/include -Iport/third_party -I$(AMIGA_SDL_INC)
  # Amiga-eigene Quellen: SDL-Zusatzbibliotheken und Netzwerk
  # amiga_startup.cpp steht in sources.mk (auf anderen Systemen wirkungslos)
  # amiga_audio.cpp steht in sources.mk: der Engine-Code ruft zod_audio_close
  # beim Beenden, und auf anderen Systemen ist das Modul wirkungslos.
  # Eigener SDL-Ersatz: eigene 8-Bit-Flaechen, eigener Blitter, eigener
  # RTG-Schirm, Eingabe aus IDCMP. -lSDL faellt damit weg.
  ENGINE_SRCS += port/amiga/sdl_addons.cpp port/amiga/net_bsdsocket.cpp \
                 port/amiga/sdl_video.cpp port/amiga/sdl_screen.cpp \
                 port/amiga/sdl_misc.cpp port/amiga/ammx.cpp \
                 port/amiga/fastcopy.cpp
  # Das Assemblermodul laeuft NICHT ueber ENGINE_OBJS: dessen patsubst kennt
  # nur %.cpp und liesse eine .s-Datei unveraendert durch. Eigene Liste,
  # eigene Regel, und $(ENGINE) nimmt sie ueber $^ mit.
  # Der Mischerkern ist reines 68020 und gehoert in ALLE drei CPU-Baeume --
  # anders als AMMX, das nur der 080er bekommt.
  ASM_SRCS      += port/amiga/musik_kern.s
  ENGINE_C_SRCS += port/amiga/musik_mischer.c port/amiga/musik_ausgabe.c
  DEFINES       += -DZOD_MIX_ASM

  ifeq ($(ZOD_AMMX),1)
    ASM_SRCS += port/amiga/ammx_blit.s
    DEFINES  += -DZOD_AMMX
  endif
  # DIAG=1: Diagnose-Fassung mit Speichersonde (malloc/free ueber den Linker
  # abgefangen). Eigener Baum, damit die normale Fassung unberuehrt bleibt:
  #   tools/build.sh amiga CPU=68080 DIAG=1
  ifeq ($(DIAG),1)
    ENGINE_SRCS += port/amiga/alloc_probe.cpp
    DEFINES     += -DZOD_ALLOC_PROBE
    LDFLAGS     += -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=realloc -Wl,--wrap=calloc
    BUILD       := $(BUILD)-diag
    ENGINE_OBJS_DIAG := 1
    # Rahmenzeiger behalten: der Stapelruecklauf der Speicherwaechter haengt
    # daran, und dieser Baum ist genau dafuer da.
    OMITFP      :=
  endif

  CXXFLAGS += $(OMITFP)
  CFLAGS   += $(OMITFP)
  ENGINE   := $(BUILD)/zod_$(CPUSUF)
else
  $(error PLATFORM muss host oder amiga sein)
endif

# Objektverzeichnis: auf dem Amiga je CPU eigenes, sonst schlicht obj/.
ifeq ($(PLATFORM),amiga)
  OBJDIR := $(BUILD)/obj-$(CPU)
  CDDIR  := $(BUILD)/cd-$(CPU)
else
  OBJDIR := $(BUILD)/obj
  CDDIR  := $(BUILD)/cd
endif

# Der Musikweg ist reines C (der Mischer laeuft spaeter auch aus einem
# eigenen Process; C++-Laufzeit hat dort nichts zu suchen). ENGINE_OBJS
# setzt nur %.cpp um -- eine .c-Datei liefe dort unveraendert durch und
# taeuschte einen Bau vor, der sie gar nicht enthaelt.
ENGINE_OBJS   := $(patsubst %.cpp,$(OBJDIR)/%.o,$(ENGINE_SRCS) $(MAIN_SRCS))
ENGINE_C_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(ENGINE_C_SRCS))
ENGINE_OBJS   += $(ENGINE_C_OBJS)
ASM_OBJS    := $(patsubst %.s,$(OBJDIR)/%.o,$(ASM_SRCS))

# Map-Editor: dieselben ENGINE_SRCS, aber die main() des Editors statt der des
# Spiels. Die Objektdateien der Bibliotheken teilen sich beide Ziele, es wird
# also nichts doppelt uebersetzt.
EDITOR      := $(BUILD)/zod_editor
EDITOR_OBJS := $(patsubst %.cpp,$(OBJDIR)/%.o,$(ENGINE_SRCS) $(EDITOR_SRCS))

.PHONY: all hello clean editor
all: $(ENGINE)

editor: $(EDITOR)

$(ENGINE): $(ENGINE_OBJS) $(ASM_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

$(EDITOR): $(EDITOR_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $(SDLFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MMD -MP -c $< -o $@

# Assembler nur fuer die AMMX-Module. GNU as kennt AMMX nicht, vasm schon.
# -Fhunk, weil diese Werkzeugkette ausschliesslich das Hunk-Format kennt
# (m68k-amigaos-objdump --info: amiga, a.out-amiga, srec -- kein ELF).
VASM      ?= vasmm68k_mot
VASMFLAGS ?= -Fhunk -m68080 -quiet -no-opt

$(OBJDIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(VASM) $(VASMFLAGS) $< -o $@

# Der Mischerkern laeuft auf 040, 060 UND 080. Mit -m68020 lehnt vasm jeden
# 080-Befehl ab -- das ist die Gegenprobe, nicht die Absicht allein.
$(OBJDIR)/port/amiga/musik_kern.o: port/amiga/musik_kern.s
	@mkdir -p $(dir $@)
	$(VASM) -Fhunk -m68020 -quiet -no-opt $< -o $@

# --- Host-Tests ---
# wiretest  = normaler Build, wiretest-swap = Big-Endian-Simulation (ZOD_WIRE_SWAPTEST)
.PHONY: wiretest hosttest shimtest launchtest mapcliptest
wiretest: $(BUILD)/wire_test $(BUILD)/wire_test_swap
	$(BUILD)/wire_test
	$(BUILD)/wire_test_swap

# alle Host-Tests: Serialisierung, Kartenformat, Archiv-Lader, Launcher-Logik
hosttest: wiretest $(BUILD)/mapfile_test $(BUILD)/mapfile_test_swap $(BUILD)/pack_test $(BUILD)/teamcolor_test launchtest mapcliptest
	$(BUILD)/mapfile_test data/Data data/game/blank_maps
	$(BUILD)/mapfile_test_swap data/Data data/game/blank_maps
	$(BUILD)/pack_test data/game/packs data/game
	cd data/game && ../../$(BUILD)/teamcolor_test

# Die reine LOGIK von ZodLaunch auf dem Host.
#
# Der Launcher ist ein MUI-Programm und meldet NICHTS ueber den seriellen
# Kanal -- ein Emulatorlauf belegt also nur, dass er startet, nicht was er
# tut. Befehlszeile und Einstellungs-Rundlauf sind aber reine
# Zeichenkettenarbeit. Geprueft wird der ECHTE Quelltext (per #include mit
# ZOD_LAUNCH_TEST), kein Nachbau.
#
# Laeuft in einem eigenen Verzeichnis: der Test schreibt ZodLaunch.cfg.
launchtest: $(BUILD)/launch_logic_test
	@mkdir -p $(BUILD)/launchtest
	cd $(BUILD)/launchtest && ../../../$(BUILD)/launch_logic_test

# Der Sicht-Zuschnitt der Karte, deterministisch.
#
# Noetig, weil ein Bildvergleich das NICHT beantworten kann: zwei Laeufe
# desselben Binaries unterscheiden sich bei 640x480 um rund 27 000
# Bildpunkte (bewegte Einheiten) -- genauso viel wie alt gegen neu.
mapcliptest: $(BUILD)/mapclip_test
	$(BUILD)/mapclip_test

$(BUILD)/mapclip_test: tests/host/mapclip_test.cpp
	@mkdir -p $(dir $@)
	$(CXX) -O1 -g -Wall $< -o $@

$(BUILD)/launch_logic_test: tests/launcher/launch_logic_test.c launcher/zodlaunch.c
	@mkdir -p $(dir $@)
	$(CC) -O1 -g -Wall -I . $< -o $@

# Pruefstand fuer die EIGENE Grafikschicht (port/amiga/sdl_video.cpp).
#
# Sie laesst sich auf dem Host uebersetzen -- sie haengt nur an unserem eigenen
# <SDL/SDL.h>, an zod_log/zod_palette und an zod_big_alloc. Damit findet der
# AddressSanitizer in Sekunden, wofuer es auf dem Amiga achtminuetige
# Emulatorlaeufe unter MuForce braucht. Genau so wurde Guru 81000005 gefunden:
# SDL_CreateRGBSurface lieferte fuer eine 32-Bit-Anforderung eine 8-Bit-Flaeche,
# und SDL_rotozoom schrieb darauf vier Byte je Pixel.
shimtest: $(BUILD)/shim_probe
	$(BUILD)/shim_probe data/game/packs

$(BUILD)/shim_probe: tests/shim/shim_probe.cpp port/amiga/sdl_video.cpp \
                     upstream/ZodEgine_Libs/QZod_DnSeparate/SDL_rotozoom.cpp \
                     port/zod_pack.cpp port/zod_palette.cpp port/zod_log.cpp \
                     port/amiga/amiga_startup.cpp
	@mkdir -p $(dir $@)
	$(CXX) -g -O1 -fsanitize=address -include cstring $(DEFINES) \
	  -I port/amiga/include -I port -I port/amiga -I . \
	  -I upstream/ZodEgine_Libs/QZod_DnSeparate \
	  $^ -o $@

# Archiv-Lader: geht denselben Weg wie die Engine (C++-Lader), nicht nur der Packer
# zod_palette.cpp fehlte hier -- das Ziel liess sich dadurch nicht binden,
# und `hosttest` scheiterte schon vor dem ersten Test. Ein Pruefmittel, das
# nicht baut, ist ein stiller Testausfall.
$(BUILD)/pack_test: tests/host/pack_test.cpp port/zod_pack.cpp port/zod_log.cpp port/zod_palette.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $(SDLFLAGS) $^ -o $@ $(LIBS)

# Teamfarben: ueberlebt die Palettentabelle den Weg durchs Archiv?
#
# Der Host liest assets/teams/*_palette.bmp direkt von der Platte, mit
# exakten Farben -- er kann den Fehler also gar nicht zeigen. Der Test
# bildet die RGB565-Umsetzung des Archivs deshalb selbst nach und prueft
# ZUSAETZLICH als Gegenprobe, dass der alte, exakte Vergleich daran
# scheitert. Ohne die waere nicht belegt, dass er den gemeldeten Fehler
# ueberhaupt abbildet.
$(BUILD)/teamcolor_test: tests/host/teamcolor_test.cpp \
                         upstream/ZodEgine_Libs/QZod_DnMap/zteam.cpp \
                         upstream/ZodEgine_Libs/QZod_DnSeparate/zsdl_opengl.cpp \
                         upstream/ZodEgine_Libs/QZod_DnSeparate/zsdl.cpp \
                         upstream/ZodEgine_Libs/QZod_DnSeparate/SDL_rotozoom.cpp \
                         port/zod_strings.cpp port/zod_log.cpp \
                         port/zod_pack.cpp port/zod_palette.cpp \
                         port/amiga/amiga_startup.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $(SDLFLAGS) $^ -o $@ $(LIBS)

$(BUILD)/mapfile_test: tests/host/mapfile_test.cpp port/zod_mapfile.cpp port/zod_wire.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $(SDLFLAGS) $^ -o $@

$(BUILD)/mapfile_test_swap: tests/host/mapfile_test.cpp port/zod_mapfile.cpp port/zod_wire.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEFINES) -DZOD_WIRE_SWAPTEST $(INCLUDES) $(SDLFLAGS) $^ -o $@

$(BUILD)/wire_test: tests/host/wire_test.cpp port/zod_wire.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $^ -o $@

$(BUILD)/wire_test_swap: tests/host/wire_test.cpp port/zod_wire.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEFINES) -DZOD_WIRE_SWAPTEST $(INCLUDES) $^ -o $@

# --- Amiga-Rauchtest (P0) ---
HELLO_OBJS := $(OBJDIR)/tests/amiga_hello/main.o $(OBJDIR)/platform/amiga/debug.o
hello: $(BUILD)/amiga_hello
$(BUILD)/amiga_hello: $(HELLO_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

# --- freien Speicher von aussen messen (vor/nach dem Spiel) ---
MEMPROBE_OBJS := $(OBJDIR)/tests/amiga_hello/mem_probe.o $(OBJDIR)/platform/amiga/debug.o
memprobe: $(BUILD)/mem_probe
$(BUILD)/mem_probe: $(MEMPROBE_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

# --- sprintf-Verhalten der Amiga-Laufzeit (Ursachensuche kaputter Dateinamen) ---
PROBE_OBJS := $(OBJDIR)/tests/amiga_hello/sprintf_probe.o $(OBJDIR)/platform/amiga/debug.o
sprintfprobe: $(BUILD)/sprintf_probe
$(BUILD)/sprintf_probe: $(PROBE_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

# --- SDL_FillRect auf dem echten Schirm zuruecklesen (Lebensbalken auf der V2) ---
FILLPROBE_OBJS := $(OBJDIR)/tests/amiga_hello/fill_probe.o $(OBJDIR)/platform/amiga/debug.o
fillprobe: $(BUILD)/fill_probe
$(BUILD)/fill_probe: $(FILLPROBE_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

# --- FPU-Befehlsformen des 040/060 (fdmul, fsmove ...) einzeln pruefen ---
# time_probe: Laeuft die Zeitquelle der Engine auf DIESER Maschine? Seit SDL
# raus ist, oeffnet der eigene Startcode timer.device -- vorher tat es SDL.
# Steht gettimeofday, steht die Spielzeit und nichts bewegt sich.
TIMEPROBE_OBJS := $(OBJDIR)/tests/amiga_hello/time_probe.o $(OBJDIR)/platform/amiga/debug.o
timeprobe: $(BUILD)/time_probe
$(BUILD)/time_probe: $(TIMEPROBE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) -noixemul $(CPUFLAGS) $^ -o $@ -ldebug -lamiga

# --- Musik ueber AHI: Kanaele, Klangplaetze und der Player-Hook (P?) ---
# Eigenstaendig, KEINE Engine-Quellen. Beantwortet vor dem Umbau der Engine,
# ob AHIA_PlayerFunc auf der Zielhardware traegt -- das kann kein Host- und
# kein Emulatorlauf. -lamiga liefert HookEntry, -lm die Halbtontabelle.
# --- Was kostet der eigene Musikmischer? Ohne AHI, ohne Paula, ohne Ausgabe.
# Gemessen wird unmittelbar die Zeit fuer eine feste Menge Musik.
# --- Hoertest fuer beide Ausgabewege: derselbe Mischer, einmal ueber AHI,
# einmal ueber Paula. Der Unterschied ist NUR der Weg zur Hardware.
ABSPIELPROBE_OBJS := $(OBJDIR)/tests/amiga_hello/abspiel_probe.o \
                     $(OBJDIR)/port/amiga/musik_ausgabe.o \
                     $(OBJDIR)/port/amiga/amiga_audio.o \
                     $(OBJDIR)/port/amiga/musik_mischer.o \
                     $(OBJDIR)/port/amiga/musik_kern.o \
                     $(OBJDIR)/port/zod_log.o \
                     $(OBJDIR)/platform/amiga/debug.o
abspielprobe: $(BUILD)/abspiel_probe
$(BUILD)/abspiel_probe: $(ABSPIELPROBE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

MISCHERPROBE_OBJS := $(OBJDIR)/tests/amiga_hello/mischer_probe.o \
                     $(OBJDIR)/port/amiga/musik_mischer.o \
                     $(OBJDIR)/port/amiga/musik_kern.o \
                     $(OBJDIR)/platform/amiga/debug.o
mischerprobe: $(BUILD)/mischer_probe
$(BUILD)/mischer_probe: $(MISCHERPROBE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

MUSICPROBE_OBJS := $(OBJDIR)/tests/amiga_hello/music_probe.o $(OBJDIR)/platform/amiga/debug.o
musicprobe: $(BUILD)/music_probe
$(BUILD)/music_probe: $(MUSICPROBE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS) -lm

FPUPROBE_OBJS := $(OBJDIR)/tests/amiga_hello/fpu_probe.o $(OBJDIR)/platform/amiga/debug.o
fpuprobe: $(BUILD)/fpu_probe
$(BUILD)/fpu_probe: $(FPUPROBE_OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

# --- MUI-3.8-Startprogramm (P8) ---
# Eigenstaendig: reines C, KEIN SDL, KEINE Engine-Quellen. muimaster.library
# wird zur Laufzeit geoeffnet; die varargs-Stubs (MUI_NewObject,
# MUI_MakeObject, MUI_Request) stehen in libmui.a der Toolchain -- daher
# -lmui VOR -lamiga. Ohne -lmui muesste man auf die Inline-Makros aus
# <inline/muimaster_lib.h> ausweichen, und die sprengen den Praeprozessor
# beim verschachtelten MUI-Stil (siehe Kommentar in launcher/zodlaunch.c).
# Die C++-Flags (-fno-exceptions/-fno-rtti) gelten hier nicht.
launcher: $(BUILD)/ZodLaunch
$(BUILD)/ZodLaunch: launcher/zodlaunch.c
	@mkdir -p $(dir $@)
	$(CC) -noixemul $(CPUFLAGS) -O2 -I$(AMIGA_SDL_INC) $< -o $@ -lmui -lamiga

# --- ZExtract: Spieldaten von der Original-CD holen ---
# Eigenstaendig wie der Launcher: reines C, keine Engine-Quellen, keine
# Bibliothek ausser dos.library und exec. tables.c ist erzeugt
# (tools/cd/gen_tables.py) und enthaelt die Zuordnung Satz -> Dateiname.
#
# Derselbe Quelltext laeuft auf dem Host gegen eine Attrappe (hostshim.c).
# Damit ist "bitgleich zum Linux-Extraktor" mit cmp pruefbar, ohne Emulator.
#
# Getrennt uebersetzt und erst dann gebunden, damit der Bauwaechter in
# tools/build.sh die Objektdatei pruefen kann: Im fertigen Hunk liegen die
# konstanten Tabellen in .text, und objdump zerlegt sie als waeren es Befehle
# -- jede Wache am Binary meldet dann Unsinn.
ZEXTRACT_OBJS := $(CDDIR)/zextract.o $(CDDIR)/tables.o \
                 $(CDDIR)/xmi2zmu.o $(CDDIR)/jmp2.o $(CDDIR)/lbm.o \
                 $(CDDIR)/schirme.o $(CDDIR)/debug.o

extract: $(BUILD)/ZExtract
$(BUILD)/ZExtract: $(ZEXTRACT_OBJS)
	@mkdir -p $(dir $@)
	$(CC) -noixemul $(CPUFLAGS) $^ -o $@ -ldebug -lamiga

# tables.c ist ERZEUGT und steht deshalb in .gitignore (1,5 MB Quelle aus
# names.txt, sounds.txt und xlat.txt). Ohne diese Regel liesse sich ZExtract
# aus einem frischen Klon nicht bauen -- der Fehler kaeme als fehlende Datei
# beim Uebersetzen, nicht als "du musst erst ein Skript laufen lassen".
tools/cd/amiga/tables.c: tools/cd/gen_tables.py tools/cd/names.txt \
                         tools/cd/sounds.txt tools/cd/xlat.txt
	python3 $<

$(CDDIR)/tables.o: tools/cd/amiga/tables.c

$(CDDIR)/%.o: tools/cd/amiga/%.c tools/cd/amiga/zextract.h
	@mkdir -p $(dir $@)
	$(CC) -noixemul $(CPUFLAGS) -O2 -Itools/cd/amiga -Iplatform/amiga -c $< -o $@

$(CDDIR)/debug.o: platform/amiga/debug.c
	@mkdir -p $(dir $@)
	$(CC) -noixemul $(CPUFLAGS) -O2 -Iplatform/amiga -c $< -o $@

clean:
	rm -rf build

-include $(ENGINE_OBJS:.o=.d) $(HELLO_OBJS:.o=.d)
