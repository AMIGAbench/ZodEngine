ZOD ENGINE -- AMIGA PORT
========================

$VER: ZodEngine README 1.0 (24.9.2026)
Author: Cem "Cego" Göktas @ 2026
Code: Claude Code 


WHAT IS THIS?
-------------

"Z" is a real time strategy game by the Bitmap Brothers, released in 1996 for
DOS. You command an army of robots, take territory zone by zone and build
units in the factories you capture.

The ZOD ENGINE is an open source re-implementation of that game, written by
Nighsoft and released under the GPLv3.
THIS is a port of the Zod Engine to AmigaOS 3.2, running on RTG (Picasso96).


No game data is included. The graphics, sounds and music belong to the Bitmap
Brothers, and they are taken from YOUR OWN original CD of "Z" during
installation. The whole extraction runs on the Amiga; no PC, no DOSBox, no
emulator is needed at any point.


What this port adds over the Zod Engine template
------------------------------------------------

The Zod Engine reproduces the game, but not everything around it. This port
puts some of that back, taken from the original CD:

   *  The ORIGINAL LOADING SCREENS -- one per planet and team colour, with
      "LEVEL 01" and the level name ("Virgin Soldiers") in the original font.

   *  The ORIGINAL STATISTICS SCREEN at the end of a round: time played,
      units killed, units lost, and the choice between Retry, Continue and
      Quit. The picture shown after a defeat is picked at random, as in the
      original.

   *  The CUTSCENES. All 63 films from the CD, in the order the original
      plays them: intro, the briefing before each planet, victory and defeat,
      outro and credits. They are played by the enclosed JV Player, which was
      exclusively developed for this port.

   *  The MUSIC, played by an event mixer with an instrument bank
      (Sourced from Aminet: ADoom_Ins.lha)

Two game modes decide how much of that you get -- see USAGE below.


REQUIREMENTS
------------

   AmigaOS         3.2 (3.1 should work, it is not tested)
   CPU             68040, 68060 or 68080 (Vampire/Apollo) with FPU
   Graphics        RTG with Picasso96. AGA is NOT supported yet.
   Memory          Fast RAM. 32 MB is comfortable; the engine holds all
                   6951 images in memory at once.
   MUI             3.8 or newer, for ZodLaunch. The game itself does not
                   need it.
   Sound           optional: AHI for a sound card or SAGA, or plain Paula
                   through audio.device. Without either the game runs
                   silently.
   Disk            about 25 MB, or about 355 MB with the cutscenes
   And             your own original CD of "Z" (Bitmap Brothers, 1996)



INSTALLATION
------------

   1. Unpack the archive:      lha x ZodEngine.lha
   2. Insert your Z CD.
   3. Double-click "Install".

The installer asks for the destination and the processor, copies everything,
optionally copies the cutscenes, and finally reads the game data from the CD.
That last step takes several minutes and says nothing in between -- that is
normal. Afterwards nothing is left to do: ZodLaunch is already set to the
build you chose.



USAGE
-----

Start ZodLaunch and press "Start". Everything is set in ZodLaunch; settings
are saved when you start the game and when you close the window.

GAME MODE -- this is the important one:

   Original Z Story Mode
       The 35 original maps in the original order, all cutscenes, and the
       statistics screen after every round. The campaign is red against blue,
       so team, bots and map selection are fixed and greyed out.

   ZodEngine Mode
       Behaves like the Zod Engine template: no cutscenes, no statistics
       screen, and your own map list, teams and bots. Only the original
       loading screen is kept.

       The reason the modes exist: the film sequence is its own progression
       through the five planets, counted up on every win. It does not know
       which map is loaded -- in the original the order is fixed, so that is
       correct there. With your own map list the films would tell a campaign
       that has nothing to do with what you are playing.

OTHER SETTINGS:

   Binary         zod_040, zod_060 or zod_080. There is deliberately no
                  default -- a wrongly guessed build only reports that it
                  will not start.
   Resolution     640x480 upwards. Max resolution 1280x720
                  Larger screens cost frame rate
   
   Scaling        caps the DRAWN size of explosion debris, 1.0 to 6.0.
                  Graphics only: flight path, flight time and impact moment
                  are unchanged. Lower values help on 68040 and 68060.
   Music output   AHI or Paula supported. CAMD/MHI support is planned.
   
   Cutscenes      turns the videos playback off

IN THE GAME:

   Arrow keys / mouse at the screen edge / minimap   scroll
   m                                                 release or grab the mouse
   ESC                                               quit (asks first), and
                                                     during a film: skip it


IN THE FUTURE
-------------

   *  NETWORK MULTIPLAYER. The port keeps the wire format of PC Zod
      (little endian, x86 layout), so an Amiga and a PC should be able to
      play together.

   *  AGA SUPPORT. Everything the engine draws already goes through one
      shared 256 colour palette, which is the hard part. What is missing is
      an AGA screen and the copy path to it. The video player already
      supports AGA.

   *  FURTHER OPTIMISATION. 68040 and 68060 builds are quick and dirty.

   *  MAP EDITOR. It is part of the source but has not been ported yet.


BUG REPORTS
-----------

   Please report bugs to amigabench AT gmail DOT com

What helps in a report:

   *  which build (zod_040, zod_060, zod_080) and which Amiga
   *  resolution and colour depth
   *  what you did, and what you expected instead
   *  the log. Start the game with  -L ZOD:log.txt  and send that file;
      on the launcher, add it under "Arguments".


THANKS
------

   To all my betatesters. You know who you are ;)


LICENCE
-------

The Zod Engine and this port are GPLv3. The source of the port is at

       https://github.com/AMIGAbench

The game data is NOT covered by that licence and is not distributed: the
graphics, sounds and music belong to the Bitmap Brothers.
