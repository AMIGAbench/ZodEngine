#include "zfont.h"

#include "zod_pack.h"
#include "zod_log.h"

ZFont::ZFont()
{
	finished_init = false;
}

void ZFont::Init()
{
	int i;
	char filename_c[500];

	for(i=0;i<MAX_CHARACTERS;i++)
        char_img[i] = nullptr;

	int loaded = 0;

	for(i=0;i<MAX_CHARACTERS;i++)
	{
        snprintf(filename_c, sizeof(filename_c), "assets/fonts/%s/char_%03d.png",
                font_type_string[type].c_str(), i);

		/* Erst im Archiv nachsehen, genau wie ZSDL_Surface::LoadBaseImage.
		 * Die Urfassung ging direkt auf IMG_Load und umging damit als einzige
		 * Stelle der Engine die Asset-Archive -- auf dem Amiga bedeutete das
		 * MAX_CHARACTERS Dateioeffnungen je Schriftart, und sie blieben
		 * stillschweigend ohne Ergebnis, weil niemand den Rueckgabewert prueft. */
		char_img[i] = zod_pack_load_quiet(filename_c);

		if(!char_img[i]) char_img[i] = IMG_Load(filename_c);

		if(char_img[i]) loaded++;
	}

	/* Ohne diese Meldung war ein Totalausfall der Schrift unsichtbar: die
	 * Knoepfe erscheinen dann einfach unbeschriftet, und im Mitschnitt steht
	 * nichts. Genau so ist es dem Nutzer auf der V2 ergangen. */
	if(!loaded)
		ZLOG("ZFont: KEIN einziges Zeichen fuer '%s' geladen -- Text bleibt unsichtbar\n",
		     font_type_string[type].c_str());

	finished_init = true;
}

SDL_Surface *ZFont::Render(const char *message)
{
	int i;
	int total_width, max_height;
	SDL_Rect to_rect;
    SDL_Surface *surface = nullptr;

    if(!finished_init) return nullptr;

	//get what the width of the img will be
	total_width = 0;
	max_height = 0;
	for(i=0;message[i];i++)
	{
		char &c = ((char*)message)[i];

		if(char_img[c]) 
		{
			total_width += char_img[c]->w;
			if(char_img[c]->h > max_height)
				max_height = char_img[c]->h;
		}
	}

    if(!total_width) return nullptr;
    if(!max_height) return nullptr;

	/* Zielflaeche mit FARBSCHLUESSEL statt Alphakanal.
	 *
	 * Die Urfassung legte eine 32-Bit-Flaeche mit SDL_SRCALPHA an und liess
	 * sie bei Alpha 0 stehen. Das ging nur gut, solange die Zeichenbilder
	 * selbst einen Alphakanal mitbrachten (SDL_image auf dem Host). Auf dem
	 * Amiga kommen sie aus dem Archiv als RGB565 mit Farbschluessel, ganz ohne
	 * Alpha -- dann blieb die Zielflaeche durchsichtig und der Text unsichtbar.
	 * Nur 8 von 9539 Bildern des Spiels haben ueberhaupt Teiltransparenz, ein
	 * Farbschluessel genuegt also und ist nebenbei deutlich billiger zu blitten. */
	{
		/* SCHWARZ als Schluessel, nicht Magenta.
		 *
		 * Magenta war sichtbar falsch: Flaechen, die spaeter MakeAlphable()
		 * durchlaufen, bekommen dort SDL_SetColorKey(..., 0x000000) gesetzt --
		 * der Magenta-Schluessel galt dann nicht mehr und der Hintergrund des
		 * Textes wurde magenta ("Loading 0%" im Ladebildschirm).
		 * Schwarz ist die Festlegung der ganzen Engine (siehe SDL_ModifyBlack)
		 * und bleibt deshalb auch nach MakeAlphable gueltig. Die Schriften sind
		 * hell auf Freiflaeche, es geht also kein Zeichenpixel verloren. */
		const Uint32 key_r = 0x00, key_g = 0x00, key_b = 0x00;
		Uint32 key;

		/* Im SCHIRMFORMAT, nicht fest in 32 Bit.
		 *
		 * Die Zeichenbilder kommen aus dem Archiv mit 8 Bit. Eine 32-Bit-
		 * Zielflaeche verlangt vom Blitter einen Weg 8 -> 32, den der
		 * Amiga-Unterbau nicht hat: Er kehrt ohne zu zeichnen zurueck, und der
		 * Text fehlt vollstaendig (am 18.09. auf der V2 gesehen -- Knoepfe
		 * ohne Beschriftung, kein "Loading 0%"). Im Schirmformat ist der Blit
		 * ein reiner Kopiervorgang mit Farbschluessel. */
		SDL_Surface *schirm = SDL_GetVideoSurface();
		const int tiefe = (schirm && schirm->format) ? schirm->format->BitsPerPixel : 32;

		surface = (tiefe == 32)
		        ? SDL_CreateRGBSurface(SDL_SWSURFACE, total_width, max_height, 32,
		                               0x00FF0000, 0x0000FF00, 0x000000FF, 0)
		        : SDL_CreateRGBSurface(SDL_SWSURFACE, total_width, max_height, tiefe,
		                               schirm->format->Rmask, schirm->format->Gmask,
		                               schirm->format->Bmask, 0);

		if(!surface) return nullptr;

		key = SDL_MapRGB(surface->format, key_r, key_g, key_b);

		SDL_FillRect(surface, nullptr, key);
		SDL_SetColorKey(surface, SDL_SRCCOLORKEY, key);
	}

	//render to it
	to_rect.x = 0;
	to_rect.y = 0;
	for(i=0;message[i];i++)
	{
		char &c = ((char*)message)[i];

		if(char_img[c]) 
		{
            SDL_BlitSurface(char_img[c], nullptr, surface, &to_rect);
			to_rect.x += char_img[c]->w;
		}
	}

	//return it
	return surface;
}

void ZFont::SetType(int type_)
{
	type = type_;
}
